//#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <PubSubClient.h>

#define WIFI_TIMEOUT 30000
#define DEBUG true
#define Serial if(DEBUG)Serial
#define MQTT_BROKER "ideapad-510-15isk.local"

//#define wifiDetailsAddress 0
//
//struct wifiDetails {
//  char SSID[32];
//  char password[64];
//};

//void storeWifiDetails(String SSID, String password) {
//  wifiDetails myWifiDetails;
//  int i = 0;
//  for(char c : SSID) {
//    myWifiDetails.SSID[i++] = c;
//  }
//  myWifiDetails.SSID[i] = '\0';
//  i = 0;
//  for(char c : password) {
//    myWifiDetails.password[i++] = c;
//  }
//  myWifiDetails.password[i] = '\0';
//  EEPROM.put(wifiDetailsAddress, myWifiDetails);
//}
//
//// Checks whether EEPROM contains useful data or random stuff
//bool checkString(String s) {
//  for(char c : s) {
//    Serial.println((int)c);
//    if(c == '\0') break;
//    if (c > 127) return false;
//  }
//  return true;
//}

// Set to the room name
String hostname;
bool setupMode = false;
// Client sends whether this is a PWM pin or not
bool isPWM[11] = {false};
//Note: Client always sends NodeMCU pin numbers
uint8_t outputPinMap[9] = {16, 5, 4, 0, 2, 14, 12, 13, 15}; //Maps Digital Pin Numbers from 0 to 8 to GPIO pin numbers
//Stores value of the output pin
int outputPinVal[9] = {0};

WiFiClient espClient;
PubSubClient localClient(espClient);

ESP8266WebServer *webServer;

//Setup the Wifi Hotspot. Used to configure the client in case of trouble.
void setupWifiAP() {
  hostname = "setup";
  WiFi.hostname(hostname);                      //Set hostname to "setup"
  Serial.println("Setting up WiFi AP");
  WiFi.softAP("NodeMCU v3");
  setupMode = true;
}

//Setup a basic webserver to communicate over, in case of configuration changes
void setupWebServer() {
  webServer = new ESP8266WebServer(80);
  webServer->on("/", handleConfig);
  webServer->begin();
}

//Handler for the HTTP Server. Assumes input in the following format:
//SSID
//Password
//Hostname
void handleConfig() { //Handler for the body path

  if (webServer->hasArg("plain") == false) { //Check if body received

    webServer->send(200, "text/plain", "Body not received");
    return;
  }
  File f = SPIFFS.open("WiFi.config", "w+");
  f.print(webServer->arg("plain"));
  f.close();
  webServer->send(200, "text/plain", "Received\n");
  ESP.reset();
}

//Payload will be 3 bytes. First byte represents pin Number, Second represents ifPWM, Third the value to set the pin at
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  for (int i = 0; i < length; i++) {
    Serial.print(payload[i]);
    Serial.print(" ");
  }
  Serial.println(length);
  //TODO: Set these values straight
  if (length == 3 && payload[0] - 48 < 9) {
    Serial.println("Inside if");
    int pin = payload[0] - 48;
    isPWM[pin] = payload[1] - 48 == 0 ? false : true;
    outputPinVal[pin] = payload[2] - 48;
    if (isPWM[pin]) analogWrite(pin, outputPinVal[pin]);
    else digitalWrite(pin, outputPinVal[pin]);
  }
}

//Initialize output pins and set them all as Low
void initializeOutputPins() {
  for (int i = 0; i < 9; i++) {
    pinMode(outputPinMap[i], OUTPUT);
    digitalWrite(outputPinMap[i], LOW);
  }
}

//Init
void setup() {
  initializeOutputPins();
  Serial.begin(9600);
  delay(10);
  //  EEPROM.begin(512);
  //  storeWifiDetails("SSID", "password");
  //  int eeAddress = 0;  //Beginning of EEPROM
  //  wifiDetails myWifiDetails;
  //  EEPROM.get(eeAddress, myWifiDetails);
  //  eeAddress += sizeof(myWifiDetails);
  //  WiFi.mode(WIFI_AP_STA);
  Serial.println();
  Serial.println("Reading config...");
  //Begin File System
  SPIFFS.begin();
  //Setup Config/setup mode (Hotspot and WebServer) in case no config exists
  if (!SPIFFS.exists("WiFi.config")) {
    setupWifiAP();
    setupWebServer();
  }
  //If config exists
  else {
    File wifiConfig = SPIFFS.open("WiFi.config", "r");      //Open the file
    String SSID = wifiConfig.readStringUntil('\n');         //Load SSID
    String password = wifiConfig.readStringUntil('\n');     //Load password
    hostname = wifiConfig.readStringUntil('\n');            //Load hostname
    WiFi.hostname(hostname);                                //Set hostname of the device for mDNS
    Serial.println("SSID: " + SSID);
    Serial.println("Password: " + password);
    Serial.println("Hostname: " + hostname);
    WiFi.begin(SSID, password);                             //Try connecting to Wifi
    unsigned long time = millis();                          //If not connected in WIFI_TIMEOUT time, go to config/setup mode
    while (WiFi.status() != WL_CONNECTED && !setupMode) {
      delay(500);
      if (millis() - time > WIFI_TIMEOUT) {
        setupWifiAP();
        setupWebServer();
      }
      Serial.print(".");
    }
    if (DEBUG && !setupMode) Serial.println("");
    if (DEBUG && !setupMode) Serial.println("WiFi connected");
    if (DEBUG && !setupMode) Serial.println("IP address: ");
    if (DEBUG && !setupMode) Serial.println(WiFi.localIP());

    localClient.setServer(MQTT_BROKER, 1883);               //Set the MQTT Server
    localClient.setCallback(mqttCallback);                  //Set the MQTT listener
  }
  if (!MDNS.begin(hostname)) {             // Start the mDNS responder for hostname.local
    Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
  if (setupMode) MDNS.addService("http", "tcp", 80);    //For Service Discovery if need be
}

//Non blocking reconnect function for MQTT
bool reconnectMQTTBroker(PubSubClient &client, String prefix) {
  Serial.print("Attempting MQTT connection...");
  client.connect(MQTT_BROKER);
  Serial.println("Connected: " + client.connected());
  if (client.connected()) {
    String topic = prefix + "/" + hostname;
    client.subscribe(topic.c_str());
  }
  return client.connected();
}

bool reconnectMQTTBroker(PubSubClient &client) {
  return reconnectMQTTBroker(client, "");
}

unsigned long int lastConnectedWifi = 0;                        //Stores the last time when the device was connected to Wifi
unsigned long int lastReconnectAttemptLocalConnection = 0;      //Stores the last time when the device attempted to connect to broker, in case of failed connection

void loop() {
  //If Wifi is disconnected, wait for it to connect
  //Or Time out and go to setup Mode
  while (WiFi.status() != WL_CONNECTED && !setupMode) {
    delay(500);
    if (millis() - lastConnectedWifi > WIFI_TIMEOUT) {
      setupWifiAP();
      setupWebServer();
    }
    Serial.print(".");
  }

  //If you are in setup mode and Wifi gets connected, switch off Setup mode
  if (setupMode && WiFi.status() == WL_CONNECTED) {
    Serial.println("Switching off setupMode");
    WiFi.softAPdisconnect(true);
    webServer->~ESP8266WebServer();
  }
  //Function to listen and respond to mDNS queries
  MDNS.update();

  //Wifi is connected (can be assured because of the conditions above)
  if (!setupMode) {
    lastConnectedWifi = millis();
    //If broker is down, try reconnecting
    if (!localClient.connected()) {
      long now = millis();
      if (now - lastReconnectAttemptLocalConnection > 1000) {
        Serial.println("Local MQTT Broker disconnected");
        lastReconnectAttemptLocalConnection = now;
        // Attempt to reconnect
        if (reconnectMQTTBroker(localClient)) {
          lastReconnectAttemptLocalConnection = 0;
        }
      }
    } else {
      //Listen for MQTT updates
      localClient.loop();
    }
  }
  //handle client connection for web server in setup mode
  if (setupMode) webServer->handleClient();
}
