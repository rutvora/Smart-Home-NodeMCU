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
//#define analogRead( analogRead(D
//#define analogWrite( analogWrite(D
//#define digitalRead( digitalRead(D
//#define digitalWrite\( digitalWrite\(D
#define MQTT_BROKER "ideapad-510-15isk.local"

//#define wifiDetailsAddress 0
//
//struct wifiDetails {
//  char SSID[32];
//  char password[64];
//};

String hostname;
bool setupMode = false;
bool isPWM[9] = {false};
int outputPinVal[9] = {0};

WiFiClient espClient;
PubSubClient localClient(espClient);

ESP8266WebServer *webServer;

void setupWifiAP() {
	hostname = "setup";
	WiFi.hostname(hostname);
	Serial.println("Setting up WiFi AP");
	WiFi.softAP("NodeMCU v3");
	setupMode = true;
}

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

void setupWebServer() {
	webServer = new ESP8266WebServer(80);
	webServer->on("/", handleConfig);
	webServer->begin();
}

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
	if (length == 3 && payload[0] - 48 < 9) {
		Serial.println("Inside if");
		int pin = payload[0] - 48;
		isPWM[pin] = payload[1] - 48 == 0 ? false : true;
		outputPinVal[pin] = payload[2] - 48;
		if (isPWM[pin]) analogWrite(pin, outputPinVal[pin]);
		else digitalWrite(pin, outputPinVal[pin]);
	}
}


void setup() {
	pinMode(5, OUTPUT);
	Serial.begin(9600);
  //  EEPROM.begin(512);
	delay(10);
  //  storeWifiDetails("SSID", "password");
	Serial.println();
	Serial.println("Reading config...");
  //  int eeAddress = 0;  //Beginning of EEPROM
  //  wifiDetails myWifiDetails;
  //  EEPROM.get(eeAddress, myWifiDetails);
  //  eeAddress += sizeof(myWifiDetails);
//	WiFi.mode(WIFI_AP_STA);
	SPIFFS.begin();
	if (!SPIFFS.exists("WiFi.config")) {
		setupWifiAP();
		setupWebServer();
	}
	else {
		File wifiConfig = SPIFFS.open("WiFi.config", "r");
		String SSID = wifiConfig.readStringUntil('\n');
		String password = wifiConfig.readStringUntil('\n');
		hostname = wifiConfig.readStringUntil('\n');
		WiFi.hostname(hostname);
		Serial.println("SSID: " + SSID);
		Serial.println("Password: " + password);
		Serial.println("Hostname: " + hostname);
		WiFi.begin(SSID, password);
		unsigned long time = millis();
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

		localClient.setServer(MQTT_BROKER, 1883);
		localClient.setCallback(mqttCallback);
	}
  if (!MDNS.begin(hostname)) {             // Start the mDNS responder for hostname.local
  	Serial.println("Error setting up MDNS responder!");
  }
  Serial.println("mDNS responder started");
  if (setupMode) MDNS.addService("http", "tcp", 80);
}

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

unsigned long int lastConnectedWifi = 0;
unsigned long int lastReconnectAttemptLocalConnection = 0;

void loop() {
	while (WiFi.status() != WL_CONNECTED && !setupMode) {
		delay(500);
		if (millis() - lastConnectedWifi > WIFI_TIMEOUT) {
			setupWifiAP();
			setupWebServer();
		}
		Serial.print(".");
	}
	if (setupMode && WiFi.status() == WL_CONNECTED) {
		Serial.println("Switching off setupMode");
		WiFi.softAPdisconnect(true);
		webServer->~ESP8266WebServer();
	}
	MDNS.update();
	if (!setupMode) {
		lastConnectedWifi = millis();
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
			localClient.loop();
		}
	}
	if (setupMode) webServer->handleClient();
}
