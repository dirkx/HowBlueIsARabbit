#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ArduinoOTA.h>

// http://www.wemos.cc/Products/d1_mini.html

// D4 == GPIO2 -- has build in LED.
// D3 == GPIO0
// D7 == GPIO13 -- meter to 3v3
//
#define PIN 13
#ifndef LED
#define LED 2
#endif

#define NAME "LapinAzzuro"

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
}

void configMeter() {
  pinMode(PIN, OUTPUT);
  setMeter(0);
}

void setMeter(float f) {
  const int MIN = 35;      // We sort of manually 'guessed this' and then tweaked it a bit.
  const int MAX = 953;     // Actual range is from 0 .. 1023 (0v .. 3v3)
  if (f < 0) f = 0.;
  if (f > 1.) f = 1.;

  // Note - we've wired the meter to the 3v3 rather than the 0v - as to make
  // it not wildly wack well beyond 100% during powerup.
  //
  unsigned dial = 1023 - f * (MAX - MIN) - MIN;
  analogWrite(PIN, dial);
  Serial.print("Meter set to "); Serial.print(dial); Serial.print(" ("); Serial.print(f); Serial.println(")");
}

void loop() {
  ArduinoOTA.handle();
  int d = millis() / 5000;
  d = d % 2;
  setMeter(d);
}
