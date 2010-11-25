/** iServer for Joggler xAP/TCP gateway
  $Id$
 
 Copyright (c) Brett England, 2010
 
 No commercial use.
 No redistribution at profit.
 All derivative work must retain this message and
 acknowledge the work of the original author.
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>

#include "xap.h"
#include "tokenize.h"

const char *inifile = "/etc/xap-livebox.ini";
const char *policyResponse="<?xml version=\"1.0\" encoding=\"UTF-8\"?><cross-domain-policy xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xsi:noNamespaceSchemaLocation=\"http://www.adobe.com/xml/schemas/PolicyFileSocket.xsd\"><allow-access-from domain=\"*\" to-ports=\"*\" secure=\"false\" /><site-control permitted-cross-domain-policies=\"master-only\" /></cross-domain-policy>";

#define ST_WAIT_FOR_MESSAGE 0
#define ST_CMD 1
#define ST_ADD_SOURCE_FILTER 2
#define ST_ADD_CLASS_FILTER 3
#define ST_XAP 4
#define ST_LOGIN 5

typedef struct _client
{
        int fd;
        char *ip;
        unsigned int txFrame;
        unsigned int rxFrame;
        char *source; // xap-heat/source from 1st heartbeat
        xAPSocketConnection *xapSocketHandler;
        xAPFilterCallback *firstHeartbeat;
        // Parser items
        unsigned char state; // Message state
        char ident[XAP_DATA_LEN+1]; // Identifier
        // LL
        struct _client *next;
}
Client;

int yylex();
YYSTYPE yylval;
Client *clientList = NULL;

// Options
char *interfaceName = "eth0";
int opt_a; // authorisation mode
int opt_b; // iServer port
int opt_c = 20; // BSC Query pacing on client filter registration (ms)
char *password;

/**************************************/

/** Send data to a file descriptor (socket)
 *
 * Due to circumstances beyond our control if the kernel decides not to send
 * all the data out in one chunk make sure it gets out there.
 *
 * The ZERO terminator will be included in the transmission.
 *
 * @param s Socket descriptor
 * @param buf Buffer to send, ZERO terminated.
 * @param len Length of buffer
 * @return -1 on failure. otherwise 0
 */
int sendAll(int s, char *buf, int len)
{
        len ++; // C strings include a NULL terminator send this on the wire too.
        int total = 0;        // how many bytes we've sent
        int bytesleft = len; // how many we have left to send
        int n;

        while(total < len) {
                n = send(s, buf+total, bytesleft, 0);
                if (n == -1) {
                        err_strerror("send");
                        break;
                }
                total += n;
                bytesleft -= n;
        }
        if(bytesleft) {
                error("Failed to send %d bytes", bytesleft);
        }

        return n==-1?-1:0; // return -1 on failure, 0 on success
}

/** Send data to a Client bumping the number of frames the client has received
 * 
 * @param c Client
 * @param buf Buffer to send, ZERO terminated.
 * @param len Length of buffer
 * @return -1 on failure. otherwise 0
 */
int sendToClient(Client *c, char *buf, int len)
{
        info("%s msg %s", c->ip, buf);
        sendAll(c->fd, buf, len);
        c->rxFrame++;
}

/** Incoming xAP packet that got past the client filters forward to the respective client.
 * 
 * @param userData pointer to client file descriptor  
 */
void xAPtoClient(void *userData)
{
        Client *c = (Client *)userData;
        char msg[XAP_DATA_LEN+12];

        strcpy(msg,"<xap>");
        xapLowerMessage();
        parsedMsgToRaw(&msg[5], sizeof(msg)-5);
        strcat(msg,"</xap>");

        sendToClient(c, msg, strlen(msg));
}

/**
 * Send a BSC query for each endpoint with subaddress Filter the client has.
 * How do we know they are BSC?
 * We don't if they aren't nobody will respond.
 *
 * @param userData pointer to client file descriptor
 */
void *sendBscQueryToFilters(void *userData)
{
        Client *c = (Client *)userData;
        xAPFilterCallback *f;
        char buff[XAP_DATA_LEN];

        LL_FOREACH(gXAP->filterList, f) {
                if(f->user_data == c) { // Any filter for this Client.
                        // We know there is only 1 FILTER in the linked list.
                        char *key = f->filter->key;
                        char *source = f->filter->value;
                        // If this is a xap-header {source=} filter and the key
                        // has a subaddress send a BSC query to it.
                        if(strcmp(key,"source") == 0 && strchr(source,':')) {
                                snprintf(buff, XAP_DATA_LEN, "xap-header\n"
                                         "{\n"
                                         "v=12\n"
                                         "hop=1\n"
                                         "uid=%s\n"
                                         "class=xAPBSC.query\n"
                                         "source=%s\n"
                                         "target=%s\n"
                                         "}\n"
                                         "request\n"
                                         "{\n"
                                         "}\n",
                                         xapGetUID(), xapGetSource(), source);
                                xapSend(buff);
                                // So we don't saturate the bus or the client handling the reply.
                                usleep(opt_c); // microseconds
                        }
                }
        }
        pthread_exit(NULL);
        die("Thread didn't exit?");
}

/** When a client first registers.
 * There are some house keeping tasks that need to be performed but we wait until
 * the heatbeat is seen so that the client is receptive to receiving data.
 * 
 * @param userData pointer to client file descriptor
 */
void firstClientHeartbeat(void *userData)
{
        Client *c = (Client *)userData;

        // Save the xAP originating source for reporting
        c->source = strdup(xapGetValue("xap-hbeat","source"));
        info("%s has source %s", c->ip, c->source);

        // remove this filter from being processed again.
        xapDelFilterAction(c->firstHeartbeat);
        c->firstHeartbeat = NULL;

        // Register a filter to send targeted messages to the client
        xAPFilter *f = NULL;
        xapAddFilter(&f,"xap-header","target", c->source);
        xapAddFilterAction(&xAPtoClient, f, c);

        // Send a BSC query to each registered filter with an endpoint.
        // With a large number of filters this could generate a serious amount of data.
        // As we are single threaded we won't be able to relay the result and as they
        // are UDP they will back up and be lost.  So spawn the QUERY into a thread.
        pthread_t queryThread;
        int rv = pthread_create(&queryThread, NULL, sendBscQueryToFilters, userData);
        if(rv) {
                error("pthread_create: ret %d", rv);
        }
}

/** Check for an already existing callback filter for this client xap source address
 * 
 * @param source xap-header source= value.
 * @param fd Client file descriptor
 * @return 1 if unique, 0 otherwise 
 */
int uniqueFilter(char *source, int fd)
{
        xAPFilterCallback *f;
        LL_FOREACH(gXAP->filterList, f) {
                if(f->callback == xAPtoClient && *(int *)f->user_data == fd) {
                        // We know there is only 1 FILTER in the linked list.
                        // So we can safely just check the first entry.
                        if(strcmp(f->filter->key,"source") == 0 && strcmp(f->filter->value,source) == 0) {
                                info("%s is a duplicate", source);
                                return 0;
                        }
                }
        }
        return 1;
}

/** Parse messages from the Joggler
//
// They look a litte like this.
// (src = iServer, dest = Joggler)
//  
// src: <ACL></ACL>
// dest: [[1]]<cmd>log<code>Joggler</cmd>
// src: <ACL>Joggler</ACL>
// dest: [[2]]<cmd><flt+>dbzoo.livebox.controller:relay.1</cmd>
// dest: [[3]]<cmd><flt+>dbzoo.livebox.controller:relay.2</cmd>
// ...
// dest: [[14]]<xap>msg</xap>
// src: <xap>msg</xap>
//  
// The [[\d+]] tokens are sent from the joggler flash application
// running in debug mode.  They can be disabled and aren't part of
// the protocol per se.
 *
 * @param fd Socket descriptor
 * @param msg Buffer to send, ZERO terminated.
 * @param len Length of buffer
 */
void parseiServerMsg(Client *c, unsigned char *msg, int len)
{
        int token;
        char *filterType;

        yy_scan_bytes(msg, len);
        while( (token = yylex()) > 0) {
                switch(c->state) {
                case ST_WAIT_FOR_MESSAGE:
                        switch(token) {
                        case YY_MSG_SEQUENCE: // protocol debug token
                                info("Mesg Sequence %d", yylval.i);
                                break;
                        case YY_START_CMD:
                                c->ident[0] = 0;
                                c->state = ST_CMD;
                                break;
                        case YY_START_XAP:
                                c->ident[0] = 0;
                                c->state = ST_XAP;
                                break;
                        default:
                                c->state = ST_WAIT_FOR_MESSAGE;
                        }
                        break;
                case ST_LOGIN:
                        switch(token) {
                        case YY_IDENT:
                                strlcat(c->ident, yylval.s, XAP_DATA_LEN);
                                break;
                        case YY_END_CMD:
	                        info("Login password %s", c->ident);
                                // If there is no authorisation or the password matched.
	                        if (opt_a == 0 || strcmp(password, c->ident) == 0) {
                                        info("Authorized");
                                        // Send code back we are good to go.
                                        char *acl = "<ACL>Joggler</ACL>";
                                        sendToClient(c, acl, strlen(acl));
                                } else {
                                        info("Access denied");
                                }
                                // drop through
                        default:
                                // On failure the iServer(windows) sends <ACL></ACL> again
                                // but if the password already failed whats the point?
                                // Just wait until the client disconnects and tries again.
                                c->state = ST_WAIT_FOR_MESSAGE;
                        }
                        break;
                case ST_CMD:
                        switch(token) {
                        case YY_LOGIN:
                                c->state = ST_LOGIN;
                                break;
                        case YY_SOURCE_FILTER:
                                c->state = ST_ADD_SOURCE_FILTER;
                                break;
                        case YY_CLASS_FILTER:
                                c->state = ST_ADD_CLASS_FILTER;
                                break;
                        default:
                                c->state = ST_WAIT_FOR_MESSAGE;
                        }
                        break;
                case ST_ADD_SOURCE_FILTER:
                case ST_ADD_CLASS_FILTER:
                        switch(token) {
                        case YY_IDENT:
	                        strlcat(c->ident, yylval.s, XAP_DATA_LEN);
                                break;
                        case YY_END_CMD:
                                filterType = c->state == ST_ADD_SOURCE_FILTER ? "source" : "class";
                                info("Add %s filter %s", filterType, c->ident);
                                if(uniqueFilter(yylval.s, c->fd)) {
                                        xAPFilter *f = NULL;
	                                xapAddFilter(&f, "xap-header", filterType, c->ident);
                                        xapAddFilterAction(&xAPtoClient, f, c);
                                }
                                // drop through
                        default:
                                c->state = ST_WAIT_FOR_MESSAGE;
                        }
                        break;
                case ST_XAP:
                        switch(token) {
                        case YY_IDENT:
	                        strlcat(c->ident, yylval.s, XAP_DATA_LEN);
                                break;
                        case YY_END_XAP:
                                c->txFrame++;
	                        xapSend(c->ident);
                                // drop through
                        default:
                                c->state = ST_WAIT_FOR_MESSAGE;
                        }
                        break;
                }
        }
}

/** Disconnect a client
 *
 * @param c Client structure
 */
void delClient(Client *c)
{
        info("Disconnecting %s / %s", c->ip, c->source);
        close(c->fd);
        xapDelSocketListener(c->xapSocketHandler);

        // Delete filters callbacks for the Client
        // As the Client* is attached as user data to the callback this
        // is used to locate the filterCallback and the filters to delete.
        // TODO: Leaky xaplib2 API abstractions are showing here.
        xAPFilterCallback *fc, *fctmp;
        LL_FOREACH_SAFE(gXAP->filterList, fc, fctmp) {
                if(fc->user_data == c) {
                        xapDelFilterAction(fc);
                }
        }

        LL_DELETE(clientList, c);
        free(c->source);
        free(c->ip);
        free(c);
}

/** Handle incoming data from the client (Joggler)
 * 
 * @param fd Client file descriptor 
 * @param data unused
 */
void clientListener(int fd, void *data)
{
        Client *c = (Client *)data;
        unsigned char buf[XAP_DATA_LEN+1];

        int bytes = recv(fd, buf, XAP_DATA_LEN, MSG_DONTWAIT);
        if(bytes == 0) {
                delClient(c);
                return;
        }

        // we got some data from the client.
        parseiServerMsg(c, buf, bytes);
}
/** Add and handle a client connection
 * 
 * @param fd Client socket file descriptor
 * @param remote sockaddr of the client
 * @return Client object
 */
Client *addClient(int fd, struct sockaddr_in *remote)
{
        char *myip = inet_ntoa(remote->sin_addr);
        Client *c = (Client *)calloc(1, sizeof(Client));
        if(c == NULL)
        {
                alert("Out of memory - Adding client from %s", myip);
                return NULL;
        }
        c->fd = fd;
        c->ip = strdup(myip);
        c->state = ST_WAIT_FOR_MESSAGE;
        c->xapSocketHandler = xapAddSocketListener(fd, &clientListener, c);
        info("Connection from %s", c->ip);
        LL_PREPEND(clientList, c);

        // There is work to do once we see a heartbeat
        xAPFilter *f = NULL;
        xapAddFilter(&f,"xap-hbeat","class","xap-hbeat.alive");

        // this fires once then deregisters itself.
        c->firstHeartbeat = xapAddFilterAction(&firstClientHeartbeat, f, c);

        // Initiate communication with client.
        char *init = "<ACL></ACL>";
        sendToClient(c, init, strlen(init));

        return c;
}

/** iServer connection handler on port 9996
 *
 * @param fd iServer descriptor
 * @param data unused 
 */
void serverListener(int fd, void *data)
{
        socklen_t addr_size;
        struct sockaddr_in remote;

        addr_size = sizeof(remote);
        int client = accept (fd, (struct sockaddr *)&remote, &addr_size);

        if(client < 0) {
                err_strerror("accept");
        } else {
                addClient(client, &remote);
        }
}

/** Policy service handler listening on port 843
 * 
 * @param fd Socket file descriptor 
 * @param data unused
 */
void flashPolicyServerHandler(int fd, void *data)
{
        socklen_t addr_size;
        struct sockaddr_in remote;
        char buf[1024];

        addr_size = sizeof(remote);
        int client = accept (fd, (struct sockaddr *)&remote, &addr_size);

        if(client < 0) {
                err_strerror("accept");
        } else {
                info("Flash policy file request from %s", inet_ntoa(remote.sin_addr));
                int bytes = recv(client, buf, sizeof(buf), 0);
                if (strcmp("<policy-file-request/>", buf) == 0) {
                        sendAll(client, (char *)policyResponse, strlen(policyResponse));
                } else {
                        notice("Unrecognized flash policy request %s", buf);
                }
                close(client);
        }
}

/** Bind to a port and open a socket
 * 
 * @param port port to bind to and open
 * @return socket file descriptor
 */
int serverBind(int port)
{
        struct sockaddr_in myaddr;     // server address
        int listener;     // listening socket descriptor
        int yes=1;        // for setsockopt() SO_REUSEADDR, below

        if(port <= 0) {
                die("Invalid port %d", port);
        }

        // get the listener
        if ((listener = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
                die_strerror("socket");
        }

        // lose the pesky "address already in use" error message
        if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes,
                        sizeof(int)) == -1) {
                die_strerror("setsockopt");
        }

        // bind
        myaddr.sin_family = AF_INET;
        myaddr.sin_addr.s_addr = INADDR_ANY;
        myaddr.sin_port = htons(port);
        memset(&(myaddr.sin_zero), '\0', 8);
        if (bind(listener, (struct sockaddr *)&myaddr, sizeof(myaddr)) == -1) {
                // don't make it fatal so we can run as a non-root user if port < 1024
                crit_strerror("Bind to port %d", port);
                return -1;
        }

        // listen
        if (listen(listener, 5) == -1) {
                die_strerror("listen");
        }
        return listener;
}

void setupXAPini()
{
        xapInitFromINI("iserver","dbzoo.livebox","iServer","00DE",interfaceName,inifile);

        opt_a = ini_getl("iserver","authmode",0,inifile);
        opt_b = ini_getl("iserver","port",9996,inifile);
        opt_c = ini_getl("iserver","pacing",20,inifile);

        if(opt_a) {
                password = getINIPassword("iserver","passwd", (char *)inifile);
                if(strlen(password) == 0) {
                        die("password hasn't been setup and we are in authorisation mode");
                }
        }
}

/// Display usage information and exit.
static void usage(char *prog)
{
        printf("%s: [options]\n",prog);
        printf("  -i, --interface IF     Default %s\n", interfaceName);
        printf("  -d, --debug            0-7\n");
        printf("  -a                     Enable authorisation mode (default off)\n");
        printf("  -b PORT                Listening port (default 9996)\n");
        printf("  -c pacing              BSC Query pacing (default 50ms)\n");
        printf("  -p PASSWD              Authorisation password\n");
        printf("  -h, --help\n");
        exit(1);
}

/** MAIN
*/
int main(int argc, char *argv[])
{
        int i;
        printf("\niServer for xAP v12\n");
        printf("Copyright (C) DBzoo 2010\n");

        for(i=0; i<argc; i++) {
                if(strcmp("-i", argv[i]) == 0 || strcmp("--interface",argv[i]) == 0) {
                        interfaceName = argv[++i];
                } else if(strcmp("-d", argv[i]) == 0 || strcmp("--debug", argv[i]) == 0) {
                        setLoglevel(atoi(argv[++i]));
                } else if(strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
                        usage(argv[0]);
                }
        }

        setupXAPini();

        // Command line override for INI
        for(i=0; i<argc; i++) {
                if(strcmp("-a", argv[i]) == 0) {
                        opt_a = 1;
                } else if(strcmp("-p", argv[i]) == 0) {
                        password = strdup(argv[++i]);
                } else if(strcmp("-b", argv[i]) == 0)  {
                        opt_b = atoi(argv[++i]);
                } else if(strcmp("-c", argv[i]) == 0)  {
                        opt_c = atoi(argv[++i]);
                }
        }
        if(opt_c < 10)
                opt_c = 10; // 10ms minimum
        opt_c *= 1000; // convert to microseconds for sleep() call.

        xapAddSocketListener(serverBind(opt_b), &serverListener, NULL);
        // The Joggler never makes such a request - MS windows flash will.
        //http://www.adobe.com/devnet/flashplayer/articles/socket_policy_files.html
        xapAddSocketListener(serverBind(843), &flashPolicyServerHandler, NULL);
        xapProcess();
        return 0;
}