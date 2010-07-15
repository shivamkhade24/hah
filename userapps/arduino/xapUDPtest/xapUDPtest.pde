// $Id$
//
// This is a demo of an Xap receiver / sender
//
// The circuit:
// * pushbutton attached to pin 7 from +5V
// * 10K resistor attached to pin 7 from ground
//
// When the button is pressed an xAPBSC.event will be sent.
// Also repond to an xAPBSC.query asking about the current button state.
//
#include <EtherCard.h>
#include <xAPEther.h>

// ethernet interface mac address
static byte mymac[6] = { 
  0x54,0x55,0x58,0x10,0x00,0x26 };
static byte myip[4] = { 
  192,168,1,15 };
static byte buf[500];

const int buttonPin = 7;
int buttonState = 0;  // current state of the button
int lastButtonState = 0;

// XAP Identification
#define UID "FFDB5500"
#define SOURCE "dbzoo.arduino.demo"
#define ENDPOINT SOURCE":print"

XapEther xap(SOURCE, UID, mymac, myip);

void setup () {
  //Serial.begin(115200);
  pinMode(buttonPin, INPUT);
  xap.setBuffer(buf, sizeof buf);
  delay(800); // ethenet chip to restart
  sendXapState("xAPBSC.info");
}

static void homePage() {
  bfill.emit_p(PSTR(
  "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/html\r\n"
    "Pragma: no-cache\r\n"
    "\r\n"
    "<title>xAP Demonstration</title>"
    "<h1>Xap Demonstration</h1>"
    "Pin $D state: $S<p>"
    "<a href=\"http://www.dbzoo.com/\">www.dbzoo.com</a>"), buttonPin, buttonState ? "HIGH" : "LOW");
}

void sendXapState(char *clazz) {
    bfill = eth.udpOffset(buf);
    bfill.emit_p(PSTR("xap-header\n"
		    "{\n"
		    "v=12\n"
		    "hop=1\n"
		    "uid=$S\n"
		    "class=$S\n"
		    "source=$S\n"
		    "}\n"
		    "output.state.1\n"
		    "{\n"
                    "state=$D\n"
                    "}"), UID, clazz, SOURCE, buttonState);
  eth.sendUdpBroadcast(buf, bfill.position(), XAP_PORT, XAP_PORT);    
}

void processXapMsg() {
  //Handy for debugging - parsed XAP message is dumped to serial port.
  //Don't forget to uncomment the Serial.begin in setup()
  //xap.dumpParsedMsg();
  if(xap.getType() != XAP_MSG_ORDINARY) return;
  if(!xap.isValue("xap-header","target", ENDPOINT)) return;

  char *clazz = xap.getValue("xap-header", "class");

  if(strcasecmp(clazz,"xAPBSC.query") == 0) {
    sendXapState("xAPBSC.info");
  }
}

void loop () {
  word len = eth.packetReceive(buf, sizeof buf);

  // ENC28J60 loop runner: handle ping and wait for a tcp packet
  word pos = eth.packetLoop(buf,len);

  // Check if valid www data is received.
  if(pos) 
  {
    bfill = eth.tcpOffset(buf);
    homePage();
    eth.httpServerReply(buf,bfill.position()); // send web page data    
  } 
  else 
  {  // Handle xAP UDP
    xap.process(len, processXapMsg);
  }
  
  // read the pushbutton input pin:
  buttonState = digitalRead(buttonPin);
  // compare the buttonState to its previous state
  if (buttonState != lastButtonState) {
    sendXapState("xAPBSC.event");
    lastButtonState = buttonState;
  }
}
