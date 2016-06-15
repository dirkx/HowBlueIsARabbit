#include <ESP8266mDNS.h>

#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ArduinoOTA.h>


//#include <EthernetUdp.h>
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

// Libby mockup
#define SERVO1 5 // for max meter left D1
#define SERVO2 4 // for max meter right D2

// Meter as currently wired on the wooden
// pedestal
// #define SERVO1 2 // for max meter left D4
// #define SERVO2 0 // for max meter right D3

#define NAME "LapinAzzuro"

char tfl_host_fqdn[64] = "api.tfl.gov.uk";
char tfl_app_id[128] = "";
char tfl_app_key[128] = "";

char met_host_fqdn[64] = "datapoint.metoffice.gov.uk";
char met_location[64] = "";
char met_key[128] = "";

const unsigned short HTTPSPORT = 443;
const unsigned short HTTPPORT = 80;

WiFiClientSecure clientTFL;
WiFiClient clientMET;

int repeat = 30 * 1000; // 30 secs repeat for testing
//int repeat = 5 * 60 * 1000; //5 mins is more reasonable for real life. or less

//callback notifying us of the need to save config
bool shouldSaveConfig = false;
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void setup() {
  configMeter();

  Serial.begin(115200);
  Serial.println("\n\n\n" NAME " Started build " __FILE__ " / " __DATE__ " / " __TIME__);


  setMeter(0.5);
  delay(2000);
  setMeter(0);
  delay(2000);
  setMeter(1);
  delay(2000);
  setMeter(0.5);


  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  if (!SPIFFS.begin()) {
    Serial.println("failed to mount FS - wiping it and then rebooting.");
    SPIFFS.format();
    ESP.restart();
    while (1);
  };

  Serial.println("mounted file system");
  if (SPIFFS.exists("/config.json")) {
    bool ok = false;
    Serial.println("reading config file");
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      Serial.println("opened config file");
      size_t size = configFile.size();
      // Allocate a buffer to store contents of the file.
      std::unique_ptr<char[]> buf(new char[size]);

      configFile.readBytes(buf.get(), size);
      DynamicJsonBuffer jsonBuffer;
      JsonObject& json = jsonBuffer.parseObject(buf.get());
      json.printTo(Serial);
      if (json.success()) {
        Serial.println("\nparsed json");

        strncpy(tfl_host_fqdn, json["tfl_host_fqdn"], sizeof(tfl_host_fqdn));
        strncpy(tfl_app_id, json["tfl_app_id"], sizeof(tfl_app_id));
        strncpy(tfl_app_key, json["tfl_app_key"], sizeof(tfl_app_key));

        strncpy(met_host_fqdn, json["met_host_fqdn"], sizeof(met_host_fqdn));
        strncpy(met_location, json["met_location"], sizeof(met_location));
        strncpy(met_key, json["met_key"], sizeof(met_key));

        ok = true;
      }
    }
    if (!ok) {
      Serial.println("failed to load json config - deleting it and rebooting");
      SPIFFS.format();
      ESP.restart();
      while (1);
    }
  }

  WiFiManager wifiManager;
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter field_tfl_server("tfl_host_fqdn", "TFL server (fqdn)", tfl_host_fqdn, sizeof(tfl_host_fqdn) - 1);
  wifiManager.addParameter(&field_tfl_server);
  WiFiManagerParameter field_tfl_app_id("tfl_app_id", "TFL APP Id", tfl_app_id, sizeof(tfl_app_id) - 1);
  wifiManager.addParameter(&field_tfl_app_id);
  WiFiManagerParameter field_tfl_app_key("tfl_app_key", "TFL APP Key", tfl_app_key, sizeof(tfl_app_key) - 1);
  wifiManager.addParameter(&field_tfl_app_key);

  WiFiManagerParameter field_met_server("met_host_fqdn", "MET server (fqdn)", met_host_fqdn, sizeof(met_host_fqdn) - 1);
  wifiManager.addParameter(&field_met_server);
  WiFiManagerParameter field_met_location("met_location", "MET LocationID", met_location, sizeof(met_location) - 1);
  wifiManager.addParameter(&field_met_location);
  WiFiManagerParameter field_met_key("met_key", "MET key", met_key, sizeof(met_key) - 1);
  wifiManager.addParameter(&field_met_key);

  // Try to connnect to the previously configured wifi network - or project a
  // configuration portal temporarily. And force a config network
  // if we have NO API keys configured.
  //
  if (!strlen(met_key) || !strlen(tfl_app_key))
    wifiManager.startConfigPortal(NAME);
  else
    wifiManager.autoConnect(NAME);

  if (shouldSaveConfig) {
    strcpy(tfl_host_fqdn, field_tfl_server.getValue());
    strcpy(tfl_app_id, field_tfl_app_id.getValue());
    strcpy(tfl_app_key, field_tfl_app_key.getValue());

    strcpy(met_host_fqdn, field_met_server.getValue());
    strcpy(met_location, field_met_location.getValue());
    strcpy(met_key, field_met_key.getValue());

    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();

    json["tfl_host_fqdn"] = tfl_host_fqdn;
    json["tfl_app_id"] = tfl_app_id;
    json["tfl_app_key"] = tfl_app_key;

    json["met_host_fqdn"] = met_host_fqdn;
    json["met_location"] = met_location;
    json["met_key"] = met_key;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }

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
    Serial.printf("Progress : % u % % \r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[ % u] : ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  Serial.print("OTA IP address : ");
  Serial.println(WiFi.localIP());
}

int getTFL() {
  if (!clientTFL.connect(tfl_host_fqdn, HTTPSPORT)) {
    Serial.println("connection failed");
    return -1;
  }


  String tfl_location1 = "1001223";
  String tfl_location2 = "1000248";

  // this url gets you a train from Peckham to Victoria nowish.
  String tfl_url = String("/Journey/JourneyResults/") + tfl_location1 +
                   " /to/" + tfl_location2 +
                   "?" +
                   "nationalSearch=False&journeyPreference=LeastTime&" +
                   "app_id=" + tfl_app_id +
                   "&app_key=" + tfl_app_key;

  Serial.print("Requesting URL : ");

  // This will send the request to the server
  String httpHeader = (String("GET ") + String(tfl_url) + " HTTP / 1.1\r\n" +
                       "Host: " + String(tfl_host_fqdn) + "\r\n" +
                       "Connection: close\r\n\r\n");

  Serial.println(httpHeader);
  clientTFL.print(httpHeader);

  unsigned long timeout = millis();
  while (clientTFL.available() == 0) {
    if (millis() - timeout > 20000) {
      Serial.println(" >>> Client Timeout !");
      clientTFL.stop();
      delay(repeat);
      return -1;
    }
  }

  // tfl data is massive
  // so we hack parse it
  int dur;

  while (clientTFL.available()) {
    //hack parsing
    String line = clientTFL.readStringUntil('}');
    //Serial.println(line);
    //now look for duration
    String duration = find_text("duration", line);
    dur = duration.toInt();
    break;
  }

  Serial.print("duration ");
  Serial.println(dur);

  float prop =  0.0;
  if(dur > 17){
        Serial.println("Disruption");
        prop = 0.33;
  }else {
        Serial.println("NO Disruption");
        prop = 0.67;
  }
  setMeter2(prop);

  Serial.println();
  Serial.println("closing connection");
  return 1;
}

String find_text(String needle, String haystack) {
  int foundpos = -1;
  for (int i = 0; i <= haystack.length() - needle.length(); i++) {
    if (haystack.substring(i, needle.length() + i) == needle) {
      foundpos = i;
      break;
    }
  }
  String duration = haystack.substring(foundpos + 10, foundpos + 12);
  return duration;
}

int getMET() {
  const int httpPort = 80;
  if (!clientMET.connect(met_host_fqdn, httpPort)) {
    Serial.println("connection failed");
    return -1;
  }

  String met_url = "/public/data/val/wxfcs/all/json/" + String(met_location) + "?res=daily&key=" + String(met_key);

  Serial.print("Requesting URL : ");
  Serial.println(met_url);

  // This will send the request to the server
  String httpHeader = (String("GET ") + met_url + " HTTP/1.1\r\n" +
                       "Host: " + met_host_fqdn + "\r\n" +
                       "Connection: close\r\n\r\n");
  Serial.println(httpHeader);
  clientMET.print(httpHeader);

  unsigned long timeout = millis();
  while (clientMET.available() == 0) {
    if (millis() - timeout > 20000) {
      Serial.println(" >>> Client Timeout !");
      clientMET.stop();
      return -1;
    }
  }

  String json;
  bool body = false;

  // Read all the lines of the reply from server and print them to Serial
  while (clientMET.available()) {
    String line = clientMET.readStringUntil('\r');
    // Serial.print(line);
    // find end of headers
    if (line == "\r\n" || line == "\r" || line == "\n" | line == "") { // "\n" is the one it actually finds
      Serial.print("empty line");
      body = true;
    }
    if (body == true) {
      json = json + line;
    }
  }
  json = json + '0'; // argh! fuck you, C!

  //Serial.println("here comes the json");
  Serial.println(String(json));
  //Serial.println("end of the json");
  char json_chars[json.length()];
  json.toCharArray(json_chars, json.length());

  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.parseObject(json_chars);
  if (!root.success()) {
    Serial.println("JSON decode failed. Ignored.");
  } else {
    Serial.println("JSON decode OK!");

    String feels_like = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][0]["FDm"];
    String preciptiation = root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"][0]["PPd"];
    Serial.println("feels like " + feels_like);
    Serial.println("rain : " + preciptiation);
    bool cold = NULL;
    bool rain = NULL;
    if(feels_like.toInt() < 10){
      cold = true;
    }else{
      cold = false;
    }
    Serial.print("is it cold? ");
    Serial.println(cold);
    if(preciptiation.toInt() > 30){
       rain = true;
    }else{
      rain = false;
    }
    Serial.print("is it going to rain? ");
    Serial.println(rain);
    // prop is proportion of the 180 degree possibility
    // this one is discrete
    float prop =  0.0;
    if(rain && cold){
      prop = 0.16;
    }else if(!rain && cold){
      prop = 0.33;
    }else if(rain && !cold){
      prop = 0.66;
    }else if(!rain && !cold){
      prop = 0.82;
    }

    setMeter1(prop);//left

  }
  Serial.println();
  Serial.println("closing connection");
  return 1;

}

#ifdef SERVO
Servo servo;
#endif
#ifdef SERVO1
Servo servo1;
#endif
#ifdef SERVO2
Servo servo2;
#endif

void configMeter() {
#if PIN
  pinMode(PIN, OUTPUT);
#endif
#if SERVO
  servo.attach(SERVO);
#endif
  setMeter(0.5);
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
  setMeter2(f);
#endif
}

#ifdef SERVO1
void setMeter1(float f) {
  if (f < 0) f = 0.;
  if (f > 1.) f = 1.;
  const int MIN = 78     // Angle 0 .. 180
  const int MAX = 160;
  unsigned dial = f * (MAX - MIN) + MIN;
  if (!servo1.attached())
  	servo1.attach(SERVO1);
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
  if (!servo2.attached())
  	servo2.attach(SERVO2);
  servo2.write(dial);
  Serial.print("Right Servo set to "); Serial.print(dial); Serial.print(" ("); Serial.print(f); Serial.println(")");
}
#endif



int i = 0, s = 1;
unsigned long last_attempt_met = 0;
unsigned long last_attempt_tfl = 0;

void loop() {
  ArduinoOTA.handle();

  if (millis() - last_attempt_tfl > repeat || last_attempt_tfl == 0) {
    int successTFL = getTFL();
    Serial.print("TFL ");
    Serial.println(successTFL);
    last_attempt_tfl = millis();
  };
  if (millis() - last_attempt_met > repeat || last_attempt_met == 0) {
    int successMET = getMET();
    Serial.print("MET ");
    Serial.println(successMET);
    last_attempt_met = millis();
  };

#ifdef SERVO1
  if (servo1.attached() && millis() - last_attempt_met > 2000)
	servo1.detatch();
#endif
#ifdef SERVO2
  if (servo2.attached() && millis() - last_attempt_met > 2000)
	servo2.detatch();
#endif

#if 0
  setMeter(i / 100.);
  i += s;
  if (i >= 100) {
    s = -1;
  }
  if (i <= 0) {
    s = 1;
  };
  delay(25 + random(120));
#endif
}
