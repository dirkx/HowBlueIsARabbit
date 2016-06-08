#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ArduinoOTA.h>

#include <EthernetUdp.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

// Only wehen controlling a servo.
#include <Servo.h>

// http://www.wemos.cc/Products/d1_mini.html
// http://www.wemos.cc/Products/d1.html

// D4 == GPIO2 -- has build in LED.
// D3 == GPIO0
// D7 == GPIO13 -- meter to 3v3
// D2 == GPIO4 Servo (https://en.wikipedia.org/wiki/Servo_control)

// #define PIN 13 // For meter sent to Libby
// #define SERVO 5 // For meter in living room

#define SERVO1 5 // for max meter left
#define SERVO2 4 // for max meter right

#ifndef LED
#define LED 2
#endif

#define NAME "LapinAzzuro"

// Just for testing - take any UDP packet on port 9133 - and
// if the json in the payload has a '{ "Blue": 0.5 }' then
// idisplay it.
//
// echo '{"lapinBlueColourness":0.2}' | nc -u <IP address> 9133
// or depending on your network setup - you can use the broadcast
// address of your network or the generic one.
//
// echo '{"lapinBlueColourness":0.2}' | nc -u 255.255.255.255 9133
WiFiUDP udp;

const unsigned short UDPPORT = 9133;
WiFiUDP Udp;

void setup() {
  pinMode(LED, OUTPUT);
  digitalWrite(LED, 1);
  configMeter();

  Serial.begin(115200);
  Serial.println("\n\n\n" NAME " Started build " __FILE__ "/" __DATE__ "/" __TIME__);
  WiFiManager wifiManager;

  // Try to connnect to the previously configured wifi network - or project a
  // configuration portal temporarily.
  //
  wifiManager.autoConnect(NAME);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    setMeter(0);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd - rebooting");
    setMeter(0.5);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    setMeter( (float) progress / total);
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

  Serial.print("OTA IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(LED, 0);

  Serial.print("Starting to listen on UDP port ");
  Serial.println(UDPPORT);
  Udp.begin(UDPPORT);
}

#if SERVO
  Servo servo;
#endif
#if SERVO1
  Servo servo1;
#endif
#if SERVO2
  Servo servo2;
#endif
  
void configMeter() {
#if PIN
  pinMode(PIN, OUTPUT);
#endif
#if SERVO
  servo.attach(SERVO);
#endif
#if SERVO1
  servo1.attach(SERVO1);
#endif
#if SERVO2
  servo2.attach(SERVO2);
#endif
  setMeter(0);
 }

void setMeter(float f) {
  if (f < 0) f = 0.;
  if (f > 1.) f = 1.;
#if PIN  
  const int MIN = 35;      // We sort of manually 'guessed this' and then tweaked it a bit.
  const int MAX = 953;     // Actual range is from 0 .. 1023 (0v .. 3v3)

  // Note - we've wired the meter to the 3v3 rather than the 0v - as to make
  // it not wildly wack well beyond 100% during powerup.
  //
  unsigned dial = 1023 - f * (MAX - MIN) - MIN;
  analogWrite(PIN, dial);
  Serial.print("Analog Meter set to "); Serial.print(dial); Serial.print(" ("); Serial.print(f); Serial.println(")");
#endif
#if SERVO
  const int MIN = 78;     // Angle 0 .. 180
  const int MAX = 160;     
  unsigned dial = f * (MAX - MIN) + MIN;
  servo.write(dial);
  Serial.print("Servo Meter set to "); Serial.print(dial); Serial.print(" ("); Serial.print(f); Serial.println(")");
#endif  
#ifdef SERVO1
 setMeter1(f);
#endif  
#ifdef SERVO2
 setMeter2(1-f);
#endif  
}

#ifdef SERVO1
void setMeter1(float f) {
  if (f < 0) f = 0.;
  if (f > 1.) f = 1.;
  const int MIN = 78;     // Angle 0 .. 180
  const int MAX = 160;     
  unsigned dial = f * (MAX - MIN) + MIN;
  servo1.write(dial);
  Serial.print("Left Servo set to "); Serial.print(dial); Serial.print(" ("); Serial.print(f); Serial.println(")");
}
#endif

#ifdef SERVO2
void setMeter2(float f) {
  if (f < 0) f = 0.;
  if (f > 1.) f = 1.;
  const int MIN = 27;     // Angle 0 .. 180
  const int MAX = 115;     
  unsigned dial = f * (MAX - MIN) + MIN;
  servo2.write(dial);
  Serial.print("Right Servo set to "); Serial.print(dial); Serial.print(" ("); Serial.print(f); Serial.println(")");
}
#endif

void parseMeter() {
  char packetBuffer[1500];
  size_t len = Udp.read(packetBuffer, sizeof(packetBuffer));

  if (len <= 0) {
    Serial.println("UDP receive failed. Ignored.");
    return;
  };

  packetBuffer[len] = '\0';
  Serial.println("Got UDP: " + (String)packetBuffer);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.parseObject(packetBuffer);

  if (!json.success()) {
    Serial.println("JSON decode failed. Ignored.");
    return;
  };

  // echo '{"lapinBlueColourness":0.5}' | nc -u <ipaddress> 9133
  const char * val = json["lapinBlueColourness"];
  if (!val) {
    Serial.println("No information about blue coloured rabbits. Ignored.");
    return;
  };
  setMeter(atof(val));
}

int i = 0, s = 1;
void loop() {
  ArduinoOTA.handle();
#if 1
   setMeter(i / 100.);
   i+=s;
   if (i >= 100) { s = -1; }
   if (i <= 0) {s = 1; };
   delay(25 + random(120));
#else
  if (Udp.parsePacket())
    parseMeter();
#endif
}
