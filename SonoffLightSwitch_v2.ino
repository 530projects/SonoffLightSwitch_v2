//These Items must be changed to match your setup
const char* ssid = "YourWifiSSID";				//Change to your Wifi Network's SSID
const char* password = "YourWifiPassword";		//Change to your Wifi Network's Password
const char* mqtt_server = "192.168.1.2";		//Change to your MQTT Brokers IP address
const char* mqtt_user = "myMQTTuser";			//Change to your MQTT User Name
const char* mqtt_pass = "MQTTme";				//Change to your MQTT User's Password

//These Items don't need to be changed, but you should make it match your mqtt scheme
#define pubTopicLight "/home/light/sonoff/01/State/"           //Light State publish Topic
#define subTopic "/home/light/sonoff/01/#"                     //Topic to subscribe to
#define LightCmd_Topic "/home/light/sonoff/01/cmd/"            //topic to recieve on/off commands
//Commands recognised are ON, OFF and RESTART

//These Items don't need to be changed, but you should make it match your OTA scheme
#define SENSORNAME "Sonoff01_LightSwitch"        	//Name for OTA
#define OTApassword "thisismyOTApassword"        	//Password for OTA
int OTAport = 8266;									//Port used for OTA

int ReportDelay = 60000;     //delay in wich it re-sends/updates its status to the mqtt broker

/*
**Items below this point do not need to be changed

Sonoff In-Wall Light Switch V2.0
This code was strung together by 530Projects, using snipits of code from others.
Because it was made with open source code snipits, it is to remain open sourced.

You can find this code at https://github.com/530projects/SonoffLightSwitch_v2
*/


#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>

#define Button3_Pin 3       //Active LOW
#define Button14_Pin 14      //Active LOW
#define Button0_Pin 0       //Active LOW
#define GreenLED_Pin 13     //Active LOW
#define Relay_Pin 12        //Active HIGH


unsigned long lastMsg = 0;
unsigned long lastAttempt = 0;
unsigned long AttemptDelay = 5000;
char* LightState = "OFF";
String strPayload;
String strTopic;
int count = 0;
int button0State;
int lastButton0State = LOW;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 80;
long rssi;
const int BUFFER_SIZE = 300;

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  pinMode(Button0_Pin, INPUT);
  pinMode(GreenLED_Pin, OUTPUT);
  digitalWrite(GreenLED_Pin, HIGH);
  pinMode(Relay_Pin, OUTPUT);
  digitalWrite(Relay_Pin, LOW);

  Serial.begin(115200);  
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  ArduinoOTA.setPort(OTAport);
  ArduinoOTA.setHostname(SENSORNAME);
  ArduinoOTA.setPassword((const char *)OTApassword);
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    digitalWrite(GreenLED_Pin, !digitalRead(GreenLED_Pin));   //Toggle LED
    checkButton();
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(GreenLED_Pin, HIGH);       //Turn Off LED when connected
}

void callback(char* topic, byte* payload, unsigned int length) {
  strPayload = "";
  for (int i = 0; i < length; i++) strPayload += (char)payload[i];
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(strPayload);
    
  strTopic = String((char*)topic);
  if (strTopic == LightCmd_Topic) {
    if (strPayload == "ON") LightOnReq();
    else if (strPayload == "OFF") LightOffReq();
    else if (strPayload == "RESTART") Restart();
  }
}

void reconnect() {
  digitalWrite(GreenLED_Pin, LOW);       //Turn On LED
  // Connect to MQTT Broker
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client";
    clientId += String(random(0xffff), HEX);
    clientId += String(millis());
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected");
      // Once connected, subscribe to the subTopic
      client.subscribe(subTopic);
      count = 0;    //reset attempt counts
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
    }
  digitalWrite(GreenLED_Pin, HIGH);       //Turn Off LED when connected
}

void loop() {
  checkButton();
  long now = millis();
  if (!client.connected()) {
    if (now - lastAttempt > AttemptDelay) {
      ++count;              //increment conntection attempts counter
      lastAttempt = now;
      reconnect();
    }
  } else client.loop();
  ArduinoOTA.handle();
  
  if (count >= 6) AttemptDelay = 30000;   	//Set the attempt delay to 30 Seconds
  if (count >= 10) Restart();				//After multiple attemps, Restart the ESP8266
  if (now - lastMsg > ReportDelay) {
    rssi = WiFi.RSSI();
    Serial.print("RSSI:");
    Serial.println(rssi);
    if (client.connected()) sendState();	//If connected to mqtt broker, send the broker our state
    lastMsg = now;
  }
}

void checkButton(){
  int reading = digitalRead(Button0_Pin);
  if (reading != lastButton0State) { 
    lastDebounceTime = millis();  // reset the debouncing timer
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {    //button has been stable longer than debounceDelay
    if (reading != button0State) {
      button0State = reading;
      if (button0State == LOW) {
        if (LightState == "OFF") LightOnReq();
        else if (LightState == "ON") LightOffReq();
      }
    }
  }
  if (button0State == LOW){  //cehck for reset request
    if ((millis() - lastDebounceTime) > 10000) Restart();     //buton pressed longer than 10 seconds, then restart
  }
  lastButton0State = reading;  // save the button reading
}

void LightOnReq(){
  Serial.println("Light On Req");
  digitalWrite(GreenLED_Pin, LOW);                      //Turn On LED
  if (LightState == "OFF"){
    digitalWrite(Relay_Pin, HIGH);
    LightState = "ON";
    if (client.connected()) sendState();
    //sendState();
  }
  delay(300);
  digitalWrite(GreenLED_Pin, HIGH);                     //Turn Off LED  
}

void LightOffReq(){
  Serial.println("Light Off Req");
  digitalWrite(GreenLED_Pin, LOW);                      //Turn On LED
  if (LightState == "ON"){
    digitalWrite(Relay_Pin, LOW);
    LightState = "OFF";
    if (client.connected()) sendState();
    //sendState();
  } 
  delay(300);
  digitalWrite(GreenLED_Pin, HIGH);                     //Turn Off LED 
}

void sendState(){
  client.publish(pubTopicLight, LightState, true);

  //The below JSON code does work, but I had troubles getting 
  //Home Assistant to pull out the LightState value as the status
  //of the light switch. I decided to get rid of the JSON style
  //payload as they only other thing I was publishing was the RSSI
  
  /*StaticJsonBuffer<BUFFER_SIZE> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();

  root["LightState"] = (String)LightState;
  root["rssi"] = (String)rssi;
  
  char buffer[root.measureLength() + 1];
  root.printTo(buffer, sizeof(buffer));

  Serial.println(buffer);
  client.publish(pubTopicLight, buffer, true);
  */
}

void Restart(){
  Serial.println("Restarting ESP8266");
  for (int j = 0; j < 10; j++){             //Blink LED quickly
    digitalWrite(GreenLED_Pin, LOW);       //Turn On LED
    delay(100);
    digitalWrite(GreenLED_Pin, HIGH);      //Turn Off LED
    delay(100);
  }
  delay(2000);
  ESP.restart();                           //Restart the ESP8266
}
