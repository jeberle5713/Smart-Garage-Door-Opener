/**
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 *  in compliance with the License. You may obtain a copy of the License at:
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
 *  on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License
 *  for the specific language governing permissions and limitations under the License.
 *
 */
 
import groovy.json.JsonSlurper

 preferences {
        input "bTextAlert", "bool", title: "Alert", description: "Alert When Door Left Open", displayDuringSetup: false
    }
 
metadata {
	definition (name: "Smart Garage Door Opener", namespace: "jeberle5713", author: "John Eberle") {
		capability "Actuator"				//No attributes, no commands.  Indicates the device has it's own commands.  ID: actuator
		capability "Door Control"			//Attributes: door.  Values: enum: closed, closing, open, opening, unknown.  Commands: close(), open()>  ID: doorControl
        capability "Garage Door Control"	//Same Attributes and Commands as door control.  ID: garageDoorControl
		capability "Contact Sensor"			//Attributes: contact: enum: closed, open.  No commands.  ID: contactSensor
		capability "Refresh"				//No attributes, Commmands: refresh().   ID: refresh
		capability "Sensor"					//No attributes or commands.  Indicates device has its own attributes.  ID: sensor
        
        command "Door2Open"
        command "Door2Close"
        
        //attribute "door2", "string"
        
	}

	simulator {
		
	}

	tiles {
		standardTile("toggle", "device.door", width: 2, height: 2) {
			state("closed", label:'${name}', action:"door control.open", icon:"st.doors.garage.garage-closed", backgroundColor:"#00A0DC", nextState:"opening")
			state("open", label:'${name}', action:"door control.close", icon:"st.doors.garage.garage-open", backgroundColor:"#e86d13", nextState:"closing")
			state("opening", label:'${name}', icon:"st.doors.garage.garage-closed", backgroundColor:"#e86d13")
			state("closing", label:'${name}', icon:"st.doors.garage.garage-open", backgroundColor:"#00A0DC")
			
		}
		standardTile("open", "device.door", inactiveLabel: false, decoration: "flat") {
			state "default", label:'open', action:"door control.open", icon:"st.doors.garage.garage-opening"
		}
		standardTile("close", "device.door", inactiveLabel: false, decoration: "flat") {
			state "default", label:'close', action:"door control.close", icon:"st.doors.garage.garage-closing"
		}
        
        standardTile("toggle2", "device.door2", width: 2, height: 2) {
			state("closed", label:'${name}', action:"Door2Open", icon:"st.doors.garage.garage-closed", backgroundColor:"#00A0DC", nextState:"opening")
			state("open", label:'${name}', action:"Door2Close", icon:"st.doors.garage.garage-open", backgroundColor:"#e86d13", nextState:"closing")
			state("opening", label:'${name}', icon:"st.doors.garage.garage-closed", backgroundColor:"#e86d13")
			state("closing", label:'${name}', icon:"st.doors.garage.garage-open", backgroundColor:"#00A0DC")
			
		}
        standardTile("open2", "device.door2", inactiveLabel: false, decoration: "flat") {
			state "default", label:'open', action:"Door2Open", icon:"st.doors.garage.garage-opening"
		}
		standardTile("close2", "device.door2", inactiveLabel: false, decoration: "flat") {
			state "default", label:'close', action:"Door2Close", icon:"st.doors.garage.garage-closing"
		}

		main "toggle"
		details(["toggle", "open", "close","toggle2","open2","close2"])
	}
}


//************************************************************************************* Event Handlers  ******************************************************

def parse(String description) {
	log.trace "parse($description)"
     def descMap = parseDescriptionAsMap(description)
     if (!state.mac || state.mac != descMap["mac"]) {
		log.debug "Mac address of device found ${descMap["mac"]}"
        updateDataValue("mac", descMap["mac"])
	}
    if (descMap["body"]) body = new String(descMap["body"].decodeBase64())
    if (body && body != "") {
    	if(body.startsWith("{") || body.startsWith("[")) {
        	def slurper = new JsonSlurper()
    		def jsonResult = slurper.parseText(body)
            // The JSON string can contain this: {"relay":{"zoneOne":"off","zoneTwo":"off",......}
            //So this part takes the relay key and parses the key:values within it to get the current
            //relay states
            if (jsonResult.containsKey("garageDoor")) {
                jsonResult.relay.each {  rel ->
                    name = rel.key	//ie. garageDoor1
                    action = rel.value	//i.e. open,closed,opening,closing
                    
                    if (action != currentVal) {
                        if (action == "open" ) {
                            isDisplayed = true
                            isPhysical = true
                         }
                         if (action == "closed") {
                            isDisplayed = false
                            isPhysical = false
                         }
                         if (action == "opening") {
                            isDisplayed = false
                            isPhysical = false
                         }
                         if (action == "closing") {
                            isDisplayed = false
                            isPhysical = false
                         }
                         //Set The value
                         def result = createEvent(name: name, value: action, displayed: isDisplayed, isStateChange: true, isPhysical: isPhysical)
                         sendEvent(result)
                    } else {
                        //log.debug "JSON No change in value"
                    }
				}	//each
        	}//if key
    	}//if body
	}//body && body
}//method

//def checkOpen() {
//	log.trace "checkOpen Called"
//}


//********************************************************************  Initialization & Update Methods  ************************************************************************************

def installed() {
	log.debug "installed()"
	configure()
}


def configure() {
	state.refreshFlag = 0;									//initialize flag to indicate not connected
	//runIn(20, "healthHandler")		//Use runIn since it halts any schedule currently on that handler
    
    log.debug "configure()"
    def cmds = []
    cmds = update_needed_settings()
    if (cmds != []) cmds
}

def updated()
{
    log.debug "updated()"
    runEvery15Minutes("update");				//Our implementation of health Check.  See Parse and healthHandler()
    def cmds = [] 
    cmds = update_needed_settings()
    sendEvent(name:"needUpdate", value: device.currentValue("needUpdate"), displayed:false, isStateChange: true)
    if (cmds != []) response(cmds)
}

//calling this will cause IP and Port to be sent to device!!!!
def update_needed_settings()
{
    def cmds = []
    def isUpdateNeeded = "NO"   
    cmds << getAction("/config?hubIp=${device.hub.getDataValue("localIP")}&hubPort=${device.hub.getDataValue("localSrvPortTCP")}")
    sendEvent(name:"needUpdate", value: isUpdateNeeded, displayed:false, isStateChange: true)
    return cmds
}


//*********************************************************************************************  Command Handlers  ************************************************************

def open() {
	//sendEvent(name: "door", value: "opening")
    //    log.trace "Running open1"
    //runIn(6, finishOpening)
    log.info "Executing 'open,1'"
    getAction("/command?command=open,1")  
}

def close() {
    sendEvent(name: "door", value: "closing")
    log.trace "Running close1"
	runIn(6, finishClosing)
     log.info "Executing 'close,1'"
    getAction("/command?command=close,1")  
}

//def finishOpening() {
//    sendEvent(name: "door", value: "open")
//    sendEvent(name: "contact", value: "open")
//}

//def finishClosing() {
//    sendEvent(name: "door", value: "closed")
//    sendEvent(name: "contact", value: "closed")
//}

def Door2Open(){
//sendEvent(name: "door2", value: "opening")

 //def result = createEvent(name: "door2", value: "opening", displayed: true, isStateChange: true)
 //sendEvent(result)
 //log.trace "Running open2"
 //runIn(6, finishOpening2)
 log.info "Executing 'open,2'"
 getAction("/command?command=open,2")  
}


def Door2Close(){
 log.info "Executing 'close,2'"
 getAction("/command?command=close,2")  
}

//def finishOpening2() {
//   // sendEvent(name: "door2", value: "open")
//    def result = createEvent(name: "door2", value: "open", displayed: true, isStateChange: true)
// sendEvent(result)
//    log.trace "Running finishopen2"
//    //sendEvent(name: "contact", value: "open")
//}

//def finishClosing2() {
//    //sendEvent(name: "door2", value: "closed")
//    def result = createEvent(name: "door2", value: "closed", displayed: true, isStateChange: true)
// sendEvent(result)
//    log.trace "Running finishclose2"
//    //sendEvent(name: "contact", value: "closed")
//}

//Start of added functions


def refresh() {
    getAction("/status")
}


def reboot() {
	log.debug "reboot()"
    def uri = "/reboot"
    getAction(uri)
}


//**************************************************************************************  LAN Support/Utility Methods  ************************************************************


//Called from Smartthings Connect App when an IP or Port change is detected from SSDP discovery
def sync(ip, port) {
    def existingIp = getDataValue("ip")
    def existingPort = getDataValue("port")
    if (ip && ip != existingIp) {
        updateDataValue("ip", ip)
        sendEvent(name: 'ip', value: ip)
    }
    if (port && port != existingPort) {
        updateDataValue("port", port)
    }
}

private encodeCredentials(username, password){
	def userpassascii = "${username}:${password}"
    def userpass = "Basic " + userpassascii.encodeAsBase64().toString()
    return userpass
}

//Performs HTTP get on uri passed.  Note response is passed back to our parse method:
//parseLanMessage example:
//
//def parse(description) {
//    ...
//    def msg = parseLanMessage(description)
//
//    def headersAsString = msg.header // => headers as a string
//    def headerMap = msg.headers      // => headers as a Map
//    def body = msg.body              // => request body as a string
//    def status = msg.status          // => http status code of the response
//    def json = msg.json              // => any JSON included in response body, as a data structure of lists and maps
//    def xml = msg.xml                // => any XML included in response body, as a document tree structure
//    def data = msg.data              // => either JSON or XML in response body (whichever is specified by content-type header in response)
//
//    ...
//}

private getAction(uri){ 
  updateDNI()
  def userpass
  log.debug uri
  if(password != null && password != "") 
    userpass = encodeCredentials("admin", password)
    
  def headers = getHeader(userpass)

  def hubAction = new physicalgraph.device.HubAction(
    method: "GET",
    path: uri,
    headers: headers
  )
  return hubAction    
}


private setDeviceNetworkId(ip, port = null){
    def myDNI
    if (port == null) {
        myDNI = ip
    } else {
  	    def iphex = convertIPtoHex(ip)
  	    def porthex = convertPortToHex(port)
        
        myDNI = "$iphex:$porthex"
    }
    log.debug "Device Network Id set to ${myDNI}"
    return myDNI
}

private updateDNI() { 
    if (state.dni != null && state.dni != "" && device.deviceNetworkId != state.dni) {
       device.deviceNetworkId = state.dni
    }
}

private getHostAddress() {
    if(getDeviceDataByName("ip") && getDeviceDataByName("port")){
        return "${getDeviceDataByName("ip")}:${getDeviceDataByName("port")}"
    }else{
	    return "${ip}:80"
    }
}

private String convertIPtoHex(ipAddress) { 
    String hex = ipAddress.tokenize( '.' ).collect {  String.format( '%02x', it.toInteger() ) }.join()
    return hex
}

private String convertPortToHex(port) {
	String hexport = port.toString().format( '%04x', port.toInteger() )
    return hexport
}


private getHeader(userpass = null){
    def headers = [:]
    headers.put("Host", getHostAddress())
    headers.put("Content-Type", "application/x-www-form-urlencoded")
    if (userpass != null)
       headers.put("Authorization", userpass)
    return headers
}

def toAscii(s){
        StringBuilder sb = new StringBuilder();
        String ascString = null;
        long asciiInt;
                for (int i = 0; i < s.length(); i++){
                    sb.append((int)s.charAt(i));
                    sb.append("|");
                    char c = s.charAt(i);
                }
                ascString = sb.toString();
                asciiInt = Long.parseLong(ascString);
                return asciiInt;
}



//def setProgram(value, program){
//   state."program${program}" = value
//}

//def hex2int(value){
//   return Integer.parseInt(value, 10)
//}




//*********************************************************************************************  General Utility Methods  **************************************************

def parseDescriptionAsMap(description) {
	description.split(",").inject([:]) { map, param ->
		def nameAndValue = param.split(":")
		map += [(nameAndValue[0].trim()):nameAndValue[1].trim()]
	}
}
