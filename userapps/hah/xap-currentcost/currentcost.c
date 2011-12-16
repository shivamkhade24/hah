/* $Id$
  Copyright (c) Brett England, 2009
   
  No commercial use.
  No redistribution at profit.
  All derivative work must retain this message and
  acknowledge the work of the original author.  
 */
#ifdef IDENT
#ident "@(#) $Id$"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <libxml/parser.h>
#include "xap.h"
#include "bsc.h"
#include "utlist.h"

// Seconds between xapBSC.info messages.
#define INFO_INTERVAL 120

const char *inifile = "/etc/xap-livebox.ini";
char serialPort[20];
int hysteresis;
char *interfaceName = "eth0";

enum {CC128, CLASSIC, ORIGINAL} model;
int currentSensor = -1;

#define ST_NONE 0
#define ST_SENSOR 1
#define ST_DATA 2
unsigned char state = ST_NONE;

bscEndpoint *endpointList = NULL;
bscEndpoint *currentTag = NULL;
bscEndpoint *ccTotal = NULL; // Endpoint to sum the Wattage over all Channels

static int infoEventChannel(bscEndpoint *, char *);
static int infoEventTemp(bscEndpoint *, char *);

// Lookup table that converts an XML tag into a bscEndpoint.
// Once a tag is matched the next CDATA section will be its value.
struct _ccTag
{
        char *xmltag;
        int (*infoEvent)(bscEndpoint *, char *);
        char *name;
        char *subaddr;
        int uid;
}
ccTag[] = {
                  {"ch1", &infoEventChannel, "ch", "1", 1},
                  {"ch2", &infoEventChannel, "ch", "2", 2},
                  {"ch3", &infoEventChannel, "ch", "3", 3},
                  {"tmpr", &infoEventTemp, "temp", NULL, 4},
                  {"tmprF", &infoEventTemp, "tempF", NULL, 4},
		  // ch.total is UID 5
		  // sensors start at 10 0x0A
                  {NULL, NULL, NULL, NULL}
          };

/// BSC callback - Only emit info/event for Channels that adjust outside of the hysteresis amount.
static int infoEventChannel(bscEndpoint *e, char *clazz)
{
        info("%s Sensor 0 %s.%s = %s", clazz, e->name, e->subaddr, e->text);
        int old = 0;
        if(e->userData)
                old = atoi((char *)e->userData);
        int new = atoi(e->text);
        // Alway report INFO events so we can repond to xAPBSC.query + Timeouts.
        // xapBSC.event are only emitted based on the hystersis
        if(strcmp(clazz, BSC_INFO_CLASS) == 0 || new > old + hysteresis || new < old - hysteresis) {
                if(e->displayText == NULL)
                        e->displayText = (char *)malloc(15);
                snprintf(e->displayText, 15, "%d Watts", new);
                return 1;
        }
	return 0;
}

/// BSC callback - Emit info/event for Temperature endpoints.
static int infoEventTemp(bscEndpoint *e, char *clazz)
{
        info("%s %s = %s", clazz, e->name, e->text);
        if(strcmp(clazz, BSC_INFO_CLASS) == 0 || e->userData == NULL || strcmp(e->text, (char *)e->userData)) {
                if(e->displayText == NULL)
                        e->displayText = (char *)malloc(15);
                char unit = strcmp(e->name,"temp") == 0 ? 'C' : 'F'; // tmpr/tmprF
                snprintf(e->displayText, 15, "Temp %s%c", e->text, unit);

                return 1;
        }
	return 0;
}

/** Load a dynamic key for the [currentcost] section from the INI file.
*
* @param key Dynamic key name
* @param sensor CurrentCost sensor we need to load data for. 1 based index.
* @param location Pointer to area for INI value
* @param size Width of the memory pointer.
*/
static long loadSensorINI(char *key, int sensor, char *location, int size)
{
        char buff[10];
        sprintf(buff,"%s%d",key,sensor);
        long n = ini_gets("currentcost", buff, "", location, size, inifile);
        info("%s=%s", buff, location);
        return n;
}

/// BSC callback - For Sensors
static int sensorInfoEvent(bscEndpoint *e, char *clazz)
{
        char unit[10];
        long n;

        info("%s %s.%s", clazz, e->name, e->subaddr);
        // note: e->userData contains the previous value (it will be NULL for the 1st data value received)
        int old = 0;
        if(e->userData)
                old = atoi((char *)e->userData);
        int new = atoi(e->text);
        char i_hysteresis[4];
        n = loadSensorINI("hysteresis", atoi(e->subaddr), i_hysteresis, sizeof(i_hysteresis));
        hysteresis = atoi(i_hysteresis);

        // Alway report INFO events so we can repond to xAPBSC.query + Timeouts.
        // xapBSC.event are only emitted based on the hystersis or if they have never been reported.
        if(strcmp(clazz, BSC_INFO_CLASS) == 0 || new > old + hysteresis || new < old - hysteresis ||
          	e->last_report == 0) {
                n = loadSensorINI("unit", atoi(e->subaddr), unit, sizeof(unit));
                if(n > 0) {
                        if(e->displayText == NULL) { // Lazy malloc
                                e->displayText = (char *)malloc(30);
                        }

                        if(e->type == BSC_BINARY) {
                                snprintf(e->displayText, 30, "%s %s", unit, bscStateToString(e));
                        } else {
                                snprintf(e->displayText, 30, "%s %s", e->text, unit);
                        }
                }
                return 1;
        }
	return 0;
}

static void findOrAddSensor(int channel)
{
        char sensor[6];
	if(channel == 1) {
	  snprintf(sensor, sizeof(sensor), "%d", currentSensor);
	} else {
	  snprintf(sensor, sizeof(sensor), "%d.%d", currentSensor, channel);
	}
	info(sensor);

        currentTag = bscFindEndpoint(endpointList, "sensor", sensor);
        if(currentTag == NULL) {
	        char stype[2] = {0}; // Analog/Digital & Reused for channel too
                loadSensorINI("channels", currentSensor, stype, sizeof(stype));
		int maxChannel = atoi(stype);
		if(maxChannel == 0 || channel <= maxChannel) {
		  unsigned char uid = (currentSensor-1)*3+channel+10;
		  info("Adding new sensor uid=%02x",uid);
		  // Add to the list we want to search and manage
		  bscSetEndpointUID(uid);
		  loadSensorINI("type", currentSensor, stype, sizeof(stype));
		  int bscType = stype[0] == 'D' ? BSC_BINARY : BSC_STREAM;
		  currentTag = bscAddEndpoint(&endpointList, "sensor", sensor, BSC_INPUT,
					      bscType, NULL, &sensorInfoEvent);
		  bscAddEndpointFilter(currentTag, INFO_INTERVAL);
		}
        }
}

/// SAX element (TAG) callback
static void startElementCB(void *ctx, const xmlChar *name, const xmlChar **atts)
{
        struct _ccTag *p;

        debug("<%s>", name);
        if(strcmp("sensor", name) == 0) {
                state = ST_SENSOR;
                return;
        }

        if (currentSensor == 0) {
                for(p=&ccTag[0]; p->xmltag; p++) {
                        if(strcmp(name, p->xmltag) == 0) {
                                state = ST_DATA;
                                currentTag = bscFindEndpoint(endpointList, p->name, p->subaddr);
                                // Dynamic endpoints. We defer creation until we see the tag in the XML
                                if(currentTag == NULL) {
                                        // Add to the list we want to search and manage
                                        bscSetEndpointUID(p->uid);
                                        currentTag = bscAddEndpoint(&endpointList, p->name, p->subaddr, BSC_INPUT, BSC_STREAM, NULL, p->infoEvent);
                                        bscAddEndpointFilter(currentTag, INFO_INTERVAL);
                                }
                        }
                }
        } else if(strcmp(name,"ch1") == 0) {
                state = ST_DATA;
                findOrAddSensor(1);
        } else if(strcmp(name,"ch2") == 0) {
                state = ST_DATA;
                findOrAddSensor(2);
        } else if(strcmp(name,"ch3") == 0) {
                state = ST_DATA;
                findOrAddSensor(3);
        }
}

// Duplicate a string removing any leading ZERO's
char *dupZero(char *s) {
	if(s == NULL || *s == '\0') // No string?
		return NULL;
	
	while(*s && *s == '0')
		s++;

	// End of String?  Must have been ALL zeros
	if(*s == '\0') {
		s--;     // One is ok then.
	}
	return strdup(s);
}

/// SAX cdata callback
static void cdataBlockCB(void *ctx, const xmlChar *ch, int len)
{
        char output[40];
        int i;

        if(state == ST_NONE)
                return;

        // Value to STRZ
        for (i = 0; (i<len) && (i < sizeof(output)-1); i++)
                output[i] = ch[i];
        output[i] = 0;

        debug("%s", output);
        switch(state) {
        case ST_SENSOR:
                currentSensor = atoi(output);
                break;
        case ST_DATA:
                // If its different report it.
                if(currentTag->text == NULL || strcmp(output, currentTag->text)) {
                        // Rotate the text data value through the user data field and free it on update.
                        if(currentTag->userData)
                                free(currentTag->userData);
                        currentTag->userData = (void *)currentTag->text;

                        // We do this to dump leading ZERO's
                        currentTag->text = dupZero(output);

                        debug("endpoint type %d", currentTag->type);
                        if(currentTag->type == BSC_BINARY) {
                                // 0 is off, 500 is ON.  We'll use any value != 0 as ON.
                                bscSetState(currentTag, atoi(currentTag->text) ? BSC_STATE_ON : BSC_STATE_OFF);
                        } else {
                                bscSetState(currentTag, BSC_STATE_ON);
                        }
                        bscSendEvent(currentTag);

			// Was this a new channel value?
			if(strcmp("ch", currentTag->name) == 0) {

			  // Sum all channels for ch.total
			  int total = 0;
			  int channelCount = 0;
			  bscEndpoint *cce;
			  LL_FOREACH(endpointList, cce) {
			    if(strcmp("ch", cce->name) == 0 && strcmp("total", cce->subaddr)) {
			      debug("ch.total adding %s.%s value %s", cce->name, cce->subaddr, cce->text);
			      total += atoi(cce->text);
			      channelCount ++;
			    }
			  }

			  // Defer construction of this endpoint until we no there are more then 3 phases.
			  if(channelCount > 1) {

			    if(ccTotal == NULL) {
			      // ch.total = ch.1 + ch.2 + ch.3
			      bscSetEndpointUID(5);
			      ccTotal = bscAddEndpoint(&endpointList, "ch", "total", BSC_INPUT, BSC_STREAM, NULL, &infoEventChannel);
			      bscAddEndpointFilter(ccTotal, INFO_INTERVAL);
			      bscSetState(ccTotal, BSC_STATE_ON);
			    }

			    // roll previous total into userData for hystersis calculations.
			    if(ccTotal->userData)
			      free(ccTotal->userData);
			    ccTotal->userData = (void *)ccTotal->text;
			    
			    char totalText[10];
			    snprintf(totalText, sizeof(totalText),"%d", total);
			    ccTotal->text = strdup(totalText);
			    
			    bscSendEvent(ccTotal);
			  }
			}
                }
                break;
        }
        state = ST_NONE;
}


static void endElementCB(void *ctx, const xmlChar *name)
{
        debug("</%s>", name);
}

/// Parse an Currentcost XML message.
void parseXml(char *data, int size)
{
        xmlSAXHandler handler;

        info("%s", data);

        memset(&handler, 0, sizeof(handler));
        handler.initialized = XML_SAX2_MAGIC;
        handler.startElement = startElementCB;
        handler.endElement = endElementCB;
        handler.characters = cdataBlockCB;

        state = ST_NONE;
        currentSensor = 0;
        currentTag = NULL;

        if(xmlSAXUserParseMemory(&handler, NULL, data, size) < 0) {
                err("Document not parsed successfully.");
                return;
        }
}

///. Read the XML stream from the currentcost unit.
void serialInputHandler(int fd, void *data)
{
        char serial_buff[128];
        static char serial_xml[4096]= {0};
        static int serial_cursor = 0;
        int i;
        int len;

        debug("(fd:%d)(len:%d)(xml:%s)", fd, serial_cursor, serial_xml);
        while((len = read(fd, serial_buff, sizeof(serial_buff))) > 0) {		
		if(getLoglevel() == LOG_DEBUG) {
			for(i=0;i<len;i++)
				putchar(serial_buff[i]);
			putchar('\n');
		}
                for(i=0; i < len; i++) {
                        if(serial_buff[i] == '\r' || serial_buff[i] == '\n')
                                continue;
                        // Prevent buffer overruns.
                        if(serial_cursor == sizeof(serial_xml)-1) {
                                serial_cursor = 0;
                        }
                        serial_xml[serial_cursor++] = serial_buff[i];
                        serial_xml[serial_cursor] = 0;

                        if(strstr(serial_xml,"</msg>")) {
				debug("EOM");
                                if(strncmp(serial_xml,"<msg>",5) == 0) {
                                        parseXml(serial_xml, serial_cursor);
                                }
				serial_cursor = 0;
				serial_xml[0] = 0;			
                        }
                }
        }
}

/// Setup the serial port.
int setupSerialPort()
{
  int fd;
  if(strncmp(serialPort,"/dev/",5) == 0) {
          struct termios newtio;
	  fd = open(serialPort, O_RDONLY | O_NDELAY);
	  if (fd < 0) {
	    die_strerror("Failed to open serial port %s", serialPort);
	  }
	  cfmakeraw(&newtio);
	  switch(model) {
	  case CC128:
	    newtio.c_cflag = B57600 | CS8 | CLOCAL | CREAD ;
	    break;
	  case ORIGINAL:
	    newtio.c_cflag = B2400 | CS8 | CLOCAL | CREAD ;
	    break;
	  default:
                newtio.c_cflag = B9600 | CS8 | CLOCAL | CREAD ;
	  }
	  newtio.c_iflag = IGNPAR;
	  newtio.c_lflag = ~ICANON;
	  newtio.c_cc[VTIME] = 0; // ignore timer
	  newtio.c_cc[VMIN] = 0; // no blocking read
	  tcflush(fd, TCIFLUSH);
	  tcsetattr(fd, TCSANOW, &newtio);
  } else {
	  fd = open(serialPort, O_RDONLY);
	  if (fd < 0) {
	    die_strerror("Failed to open file %s", serialPort);
	  }
  }
  return fd;
}

/// Display usage information and exit.
static void usage(char *prog)
{
        printf("%s: [options]\n",prog);
        printf("  -i, --interface IF     Default %s\n", interfaceName);
        printf("  -s, --serial DEV       Default %s\n", serialPort);
        printf("  -m, --model [CLASSIC|CC128|ORIGINAL]  Default CLASSIC\n");
        printf("  -d, --debug            0-7\n");
        printf("  -h, --help\n");
        exit(1);
}

/// Process the INI file for xAP control data and setup XAP.
void setupXap()
{
        char defaultPort[20];
        strcpy(defaultPort, serialPort);
  
        xapInitFromINI("currentcost","dbzoo.livebox","CurrentCost","00DC",interfaceName,inifile);

        hysteresis = ini_getl("currentcost", "hysteresis", 10, inifile);
        ini_gets("currentcost","usbserial",defaultPort, serialPort, sizeof(serialPort), inifile);

        char model_s[20];
        model = CLASSIC;
        ini_gets("currentcost","model","classic", model_s, sizeof(model_s), inifile);
        if(strcasecmp(model_s,"CC128") == 0) {
                info("Selecting CC128 model");
                model = CC128;
        } else if(strcasecmp(model_s,"ORIGINAL") == 0) {
                info("Selecting CLASSIC ORIGINAL model");
                model = ORIGINAL;
        } else {
                info("Selecting CLASSIC model");
        }
}

/// Setup Endpoints, the Serial port, a callback for the serial port and process xAP messages.
int main(int argc, char *argv[])
{
        int i;
        printf("\nCurrent Cost Connector for xAP v12\n");
        printf("Copyright (C) DBzoo, 2009-2010\n\n");
        strcpy(serialPort,"/dev/ttyUSB0");

        for(i=0; i<argc; i++) {
                if(strcmp("-i", argv[i]) == 0 || strcmp("--interface",argv[i]) == 0) {
                        interfaceName = argv[++i];
                } else if(strcmp("-s", argv[i]) == 0 || strcmp("--serial", argv[i]) == 0) {
                        strlcpy(serialPort, argv[++i], sizeof(serialPort));
                } else if(strcmp("-d", argv[i]) == 0 || strcmp("--debug", argv[i]) == 0) {
                        setLoglevel(atoi(argv[++i]));
                } else if(strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
                        usage(argv[0]);
                }
        }

        setupXap();

        // The model switch if provided overrides the INI setting.
        for(i=0; i<argc; i++) {
                if(strcmp("-m", argv[i]) == 0 || strcmp("--model",argv[i]) == 0) {
                        if(strcasecmp(argv[++i], "CC128") == 0) {
                                info("Command line override selecting CC128 model");
                                model = CC128;
                        } else if(strcasecmp(argv[++i], "ORIGINAL") == 0) {
	                        info("Command line override selecting CLASSIC ORIGINAL model");
	                        model = ORIGINAL;
                        }
                }
        }

	if(strncmp(serialPort,"/dev/",5) == 0) {
	  xapAddSocketListener(setupSerialPort(), &serialInputHandler, endpointList);
	  xapProcess();
	} else {
	  serialInputHandler(setupSerialPort(), NULL);
	}
	return 0;
}
