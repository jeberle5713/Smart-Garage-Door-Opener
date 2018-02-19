
/**
 ****************************************************************************************************************************
   ESP8266 Smart Garage Door  (2 Door Control)

   Date: 2-17-2018

   
   Garage Door Controller  controller that takes advantage of SmartThings ecosystem.
   

 ****************************************************************************************************************************
   Libraries:
   Timer library was created by Simon Monk as modified by JChristensen  https://github.com/JChristensen/Timer.  Note: if you
   download the library from the source, you must rename the zip file to "Timer" before importing into the Arduino IDE.

 ****************************************************************************************************************************
    Copyright 2018 John Eberle (jeberle5713@gmail.com)(TractorEnvy.Com)

    Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
    in compliance with the License. You may obtain a copy of the License at:

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
    on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License
    for the specific language governing permissions and limitations under the License.
 *********************************************************************************************************************

  Typical Flows:
  1) Smartthings sends an HTTP command like: /command?command=on,1,5
  2) handleCommand() is called (registered as handler for that command
  3) calls messagecallout which adjusts the queue and station times per message.  Calls toggeloff, toggleon, alloff, allon, etc
      which will set/reset trafficcop ads needed.  When ToggleOn is called, it sets a timer w/ station time setpoint with a callback
      of Toggleoff
  4) calls scheduleupdate which sets a flag to do an update next loop
  5) Calls queuemanager to turn on next station (if none currently on TrafficCop = 0)
  6)SendStates Called to send queue to PIC to drive I/O

  Main Loop
  1) if doUpdate set, calls SendNotify to tell Smartthings of current values in JSON format
  2) call Sendstates to drive I/o w/ PIC


  What happens when station timer runs out?
  ToggleOff callback is called.  Then scheduleupdate is called.


  Every minute (timer set) queuemanager is called
  sets on next availiable station and calls toggleon which turns on output, sets run timer, and calls qscheduleupdate

  Every 10 minutes (timer set) calls timeToUpdate which calls scheduleupdate

*/

#include <FS.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ESP8266SSDP.h>
#include <Timer.h>


const char* version = "1.0.0";

//******************  User configurable global variables to set before loading to Arduino******************************************
boolean isDebugEnabled = true;  // enable or disable debug in this example
int DebugPriority = 1;          //Print Debug Messages of level 1


//******************************************* Const Global variables ****************************************************************
const char APPSETTINGS[] PROGMEM = "/appSettings.json";
const char LOADED[] PROGMEM = " loaded: ";
const char HUBPORT[] PROGMEM = "hubPort";
const char HUPIP[] PROGMEM = "hubIp";
const char DEVICENAME[] PROGMEM = "deviceName";



//*************************************** Smartthings hub information  **********************************************************
IPAddress hubIp = INADDR_NONE; // smartthings hub ip
unsigned int hubPort = 0; // smartthings hub port
String deviceName = "Smart Garage 2 Door Controller";
uint8_t SSDPSuccess;


//****************************************** OTA Support ************************************************************************
const char* serverIndex = "<form method='POST' action='/updateOTA' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";

//****************************************** Irrigation Globals *****************************************************************
Timer tmr;


struct Status {
  uint8_t door1State;
  uint8_t door2State;
} RunStatus;








//Externs
//extern bool receiveComplete;    //Indictes we have gotten a serial reception from PIC
//extern bool receiveOverrunError;
//extern uint8_t lastPICMessageID;

ESP8266WebServer server(80);
WiFiClient client; //client


IPAddress IPfromString(String address) {
  int ip1, ip2, ip3, ip4;
  ip1 = address.toInt();
  address = address.substring(address.indexOf('.') + 1);
  ip2 = address.toInt();
  address = address.substring(address.indexOf('.') + 1);
  ip3 = address.toInt();
  address = address.substring(address.indexOf('.') + 1);
  ip4 = address.toInt();
  return IPAddress(ip1, ip2, ip3, ip4);
}



//Sends the message as a string to debug port.  The priority is checked against the global
//Priority setting.  If it is equal or less (i.e. a 1 priority) then it is sent.  If greater
//then not sent.  Typically, set priority = 1 for errors, 2 or lower for info / troubleshooting
void SendMsg(String msg,uint8_t priority)
{
    Serial.print(msg);
    //Serial.println(msg);
  //if(priority <= DebugPriority)
  //    SendStringMsg(msg);
}

//When the  Web Server is called with no query parameters, retuns the menu of actions
void handleRoot() {
  String rootStatus;
  rootStatus = "</style></head><center><table><TH colspan='2'>ESP8266 Smart 2 Door Garage Controller<TR><TD><TD><TR><TD colspan='2'>";
  rootStatus += "<TR><TD><TD><TR><TD>Main:<TD><a href='/update'>Firmware Update</a><BR><a href='/status'>Status</a><BR><a href='/reboot'>Reboot</a><BR></table><h6>ESP8266 Smart 2 Door Garage Controller</h6></body></center>";
  server.send(200, "text/html", rootStatus);
}

// When the Web Server is called with /command query ie on/off messages from Smartthings (or elsewhere)
void handleCommand() {
  String message = server.arg("command");     //.arg(key) accesses the parameter associated with the specified key (command)

  if (isDebugEnabled) {
    SendMsg("Got command: ",2);
    SendMsg(message,2);
  }
  parseCommand(message);
  String updateStatus = makeUpdate();
  server.send(200, "application/json", updateStatus);

}


//Handles a /status query from client.
void handleStatus() {
  //SendPICStatusRequest();
  //PIC sends back ESP_STATUS_RESPONSE Message in main loop
}

//Handle /reboot query from client.  Turns all sprinkler relays off, sends out the status and restarts
void handleReboot() {
  //SendPicRebootMessage();
  //PIC processes allOff, sends updated status w ESP_REBOOT_CONFIRM Message
}


//Send real time info
//void handleRTI() {
//  String updateStatus =  makeRTI();                      //Build JSON Object of current status ie: Zone, status etc.
//  server.send(200, "application/json", updateStatus);     //Update client
//}


void handleConfig() {
  StaticJsonBuffer<100> jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();

  //If we have received an ST IP then update as needed
  if (server.hasArg("hubIp")) {
    json[FPSTR(HUPIP)] = server.arg("hubIp");
    IPAddress newHubIp = IPfromString(json[FPSTR(HUPIP)]);
    if (newHubIp != hubIp) {
      hubIp = newHubIp;
    }
  }

  //If we have received an ST Port then update as needed
  if (server.hasArg("hubPort")) {

    unsigned int newHubPort = atoi(server.arg("hubPort").c_str());
    json[FPSTR(HUBPORT)] = newHubPort;
    if (newHubPort != hubPort) {
      hubPort = newHubPort;
    }
  }

  //save config
  String settingsJSON;
  json.printTo(settingsJSON);
  if (isDebugEnabled) {
    SendMsg("Saving JSON Configuration Settings",2);
  }
  saveAppConfig(settingsJSON);

  String updateStatus = makeUpdate();                   //Build JSON Object of current status ie: Zone, status etc.
  server.send(200, "application/json", updateStatus);   //Update (Smartthings) with current status of all Zones etc via JSON object
}


void handleNotFound() {
  server.send(404, "text/html", "<html><body>Error! Page Not Found!</body></html>");
}



void setup(void) {
  char msg[80];
  
  delay(10000); //JE

  if (isDebugEnabled) {
    // setup debug serial port
    Serial.begin(57600);         // setup serial with a baud rate of 57600
    SendMsg("setup..",2);  // print out 'setup..' on start
    sprintf(msg,"Version: %s",version);
    SendMsg(msg,2);
  }

  if (isDebugEnabled) {
    SendMsg(F("Mounting FS..."),2);
  }
  if (!SPIFFS.begin()) {
    if (isDebugEnabled) {
      SendMsg(F("Failed to mount file system"),1);
    }
    return;
  }

  // DEBUG: remove all files from file system
  //  SPIFFS.format();

  //Try to load previous application configuration.  If this is first time, or some other issue,
  //then print message.  Note: app config has prior WiFi SSID, password, etc.
  if (!loadAppConfig()) {
    if (isDebugEnabled) {
      SendMsg(F("Failed to load application config"),1);
    }
  } else {
    if (isDebugEnabled) {
      SendMsg(F("Application config loaded"),2);
    }
  }

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset saved settings
  wifiManager.resetSettings();


  //Fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "SmartGarage2Door"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("SmartGarage2Door");



  //if you get here you have connected to the WiFi
  if (isDebugEnabled) {
    sprintf(msg,"Connected. Local IP is: %s",WiFi.localIP().toString().c_str());
    SendMsg(msg,1);
  }
  
  // **************  Set up Web Server callbacks when the webserver receives a message with a specific "/xxx"
  server.on("/", handleRoot);
  server.on("/command", handleCommand);         //Handle on/off commands from Smartthings
  server.on("/status", handleStatus);           //Handle status request from smartthings
  
  server.on("/update", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/html", serverIndex);
  });
  
 server.on("/updateOTA", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      if (isDebugEnabled) {
        Serial.setDebugOutput(true);
      }
      WiFiUDP::stopAll();
      if (isDebugEnabled) {
        //Serial.printf("Update: %s\n", upload.filename.c_str());
      }
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        if (isDebugEnabled) {
          Update.printError(Serial);
        }
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        if (isDebugEnabled) {
          Update.printError(Serial);
        }
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        if (isDebugEnabled) {
          //Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        }
      } else {
        if (isDebugEnabled) {
          Update.printError(Serial);
        }
      }
      if (isDebugEnabled) {
        Serial.setDebugOutput(false);
      }
    }
    yield();
  });

  //Sent during SSDP Discovery from Smartthings.  Sends the SSDP Schema settings
  server.on("/esp8266ic.xml", HTTP_GET, []() {
    if (isDebugEnabled) {
      SendMsg("Request for /esp8266ic.xml",2);
    }
    SSDP.schema(server.client());
  });
  server.on("/config", handleConfig);
  server.on("/reboot", handleReboot);
  server.onNotFound(handleNotFound);


  server.begin();

  //SSDP Schema settings
  SSDP.setSchemaURL("esp8266ic.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName("Smart Garage 2 Door Controller");
  SSDP.setSerialNumber("0000000000001");
  SSDP.setURL("index.html");
  SSDP.setModelName("ESP8266_SMART_2_DOOR_GARAGE_CONTROLLER");  //This is checked by Smartthings
  SSDP.setModelNumber("1");
  SSDP.setModelURL("https://github.com/jeberle5713/");
  SSDP.setManufacturer("John Eberle");
  SSDP.setManufacturerURL("https://TractorEnvy.com");
   //Note:  The ESP8266 SSDP library uses a default search string (deviceType) of: urn:schemas-upnp-org:device:Basic:1
   //We didn't change this and will discriminate devices based on other schema information ie. modelName later afetr
   //the initial discovery phase.  We could change these to any urn of our liking: ie. urn:schemas-upnp-org:device:GarageDoorOpener:1
   //If this is changed, it needs to be changed for both this code  (SSDP.setdeviceType) as well as in the Smartthings Connect App 

  SSDPSuccess = SSDP.begin();   //Enable SSDP Discovery
  
  //StartSerialReceive(); //Start Checking for messages from PIC
}

void loop(void) {

  String updateStatus;
  uint8_t command;
  //tasks

  tmr.update();   //Called periodically to update timers
  server.handleClient();  //Handles incomming HTTP Requests
  //ManageSerialReceive();  //Handle Serial Reception Tasks form PIC
  
  if ( isSerialReady() )
  {    
  }
  delay(1);   //was 5 ????
}



//Returns true if a message is availiable
bool isSerialReady()
{
  
 
  return false;
}



//process incoming messages from SmartThings hub 
 void parseCommand(String message)  
 { 
   char* inValue[10];         //array holds any values being delivered with message and NULL;  Make size bigger than we need (2 should be enough) 
   char delimiters[] = ",";   //comma are only delimiters
   char charMessage[100];     //Allocate some space
   uint8_t newState;
   //a message could look like "open,1" (open door 1 )

    
  //a message could look like "on,2,5" (turn station 2 on for 5 minutes)
   
   strncpy(charMessage, message.c_str(), sizeof(charMessage)); //get into standard string (character array for processing)
   charMessage[sizeof(charMessage) - 1] = '\0';  //Ensure null terminated

  //Tokenize string and return pointer to first substring.   Ex: open
  //Now, subsequent calls to strtok with NULL will return
  //pointers to remaining parts until all are transversed and will
  //then return NUL
  inValue[0] = strtok(charMessage,delimiters);  //remove first substring as messageType 

   //Iterate through all substrings and store parameters
   int i=1; 
   while(inValue [i-1] != NULL) { 
     inValue [i] = strtok(NULL, delimiters); //remove remaining substrings as incoming values 
     i++; 
   } 

  //Now have substrings (ie. [0] = open, [1] = 1

   if (strcmp(inValue[0],"allClosed")==0) { 
      RunStatus.door2State = 0;
       RunStatus.door2State = 0;
   } 
   else {
     newState = 0;
     if (strcmp(inValue[0],"open")==0)  {   // add new station to queue 
      newState = 3;   //Make state open
     } 
  
     else if (strcmp(inValue[0],"close")==0) { 
       newState = 1;
     }
     //Get door number
    if (strcmp(inValue[1],"1")==0) { 
     RunStatus.door1State = newState;
    }
    if (strcmp(inValue[1],"2")==0) { 
         RunStatus.door2State = newState;
    }
   }
  
 } 



// send json data to client connection
void sendJSONData(WiFiClient client) {
  String updateStatus = makeUpdate(); 
  client.println(F("CONTENT-TYPE: application/json"));
  //client.println(F("CONTENT-LENGTH: 29"));
  client.println();
  client.println(updateStatus);
}

// send data
int sendNotify() //client function to send/receieve POST data.
{
  int returnStatus = 1;
  char msg[80];
  String responseString = String("Smartthings Post Response: ");

  if (client.connect(hubIp, hubPort)) {
    client.println(F("POST / HTTP/1.1"));
    client.print(F("HOST: "));
    client.print(hubIp);
    client.print(F(":"));
    client.println(hubPort);
    sendJSONData(client);
    if (isDebugEnabled) {
      SendMsg(F("Pushing new vals to hub..."),2);
    }
  }
  else {
    //connection failed
    returnStatus = 0;
    if (isDebugEnabled) {
      SendMsg(F("ST Post Connection to hub failed."),1);
    }
  }

  // read any data returned from the POST
  while (client.connected() && !client.available()) delay(1); //waits for data
  while (client.connected() || client.available()) { //connected or data available
    char c = client.read();
    responseString += c;
  }
  if (isDebugEnabled) {
    SendMsg("Returning form pushing new values",2);
    sprintf(msg,"ST Response: %s",responseString.c_str());
    SendMsg(msg,2);
  }

  delay(1);
  client.stop();
  return returnStatus;
}

String makeDoorStatus(uint8_t doorState)
{
  String retval;
  retval = "unknown";
  switch(doorState)
  {
    case 0:
      retval = "closed";
    break;
    case 1:
     retval = "opening";
    break;
    case 2:
     retval = "closing";
    break;
    case 3:
     retval = "open";
    break;
  }
}

//Called to form JSON string indicating all the different hardware states on/off/queued, mA current, etc.
String makeUpdate()
{
  StaticJsonBuffer<125> jsonBuffer;   //
  JsonObject& json = jsonBuffer.createObject();

  // builds a status update to send to SmartThings hub
  String action = "";
  String key = "";
  String statusUpdate = "";

  json["door"] = makeDoorStatus(RunStatus.door1State);    //opening, closing
  json["door2"] = makeDoorStatus(RunStatus.door2State);
  json["version"] = version;
  if (hubPort != 0) {
  json["hubconfig"] = "true";
  } else {
    json["hubconfig"] = "false";
  }

  json.printTo(statusUpdate);
  return statusUpdate;

}




bool loadAppConfig() {
  File configFile = SPIFFS.open(FPSTR(APPSETTINGS), "r");
  if (!configFile) {
    if (isDebugEnabled) {
      SendMsg(F("Failed to open config file"),1);
    }
    return false;
  }

  size_t size = configFile.size();
  if (size > 1024) {
    if (isDebugEnabled) {
      SendMsg(F("Config file size is too large"),1);
    }
    return false;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);
  configFile.close();

  const int BUFFER_SIZE = JSON_OBJECT_SIZE(3);
  StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(buf.get());

  if (!json.success()) {
    if (isDebugEnabled) {
      SendMsg(F("Failed to parse application config file"),1);
    }
    return false;
  }

  hubPort = json[FPSTR(HUBPORT)];

  String hubAddress = json[FPSTR(HUPIP)];

  hubIp = IPfromString(hubAddress);
  String savedDeviceName = json[FPSTR(DEVICENAME)];
  deviceName = savedDeviceName;
  if (isDebugEnabled) {
    SendMsg(FPSTR(HUBPORT),2);
    SendMsg(FPSTR(LOADED),2);
    //SendMsg(hubPort);
    SendMsg(FPSTR(HUPIP),2);
    SendMsg(FPSTR(LOADED),2);
    SendMsg(hubAddress,2);
    SendMsg(FPSTR(DEVICENAME),2);
    SendMsg(FPSTR(LOADED),2);
    SendMsg(deviceName,2);
  }
  return true;
}

bool saveAppConfig(String jsonString) {
  if (isDebugEnabled) {
    SendMsg(F("Saving new settings: "),1);
    SendMsg(jsonString,1);
  }
  File configFile = SPIFFS.open(FPSTR(APPSETTINGS), "w");
  if (!configFile) {
    if (isDebugEnabled) {
      SendMsg(F("Failed to open application config file for writing"),1);
    }
    return false;
  }
  configFile.print(jsonString);
  configFile.close();
  return true;
}




