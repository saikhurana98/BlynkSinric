#include <FS.h>          //this needs to be first, or it all crashes and burns...
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <BlynkSimpleEsp8266.h>
#include <IRremoteESP8266.h>
#include <IRac.h>
#include <IRutils.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

//Global intialization
WiFiManager wifiManager;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
//Pinouts & Layouts
//Device Pinout
#define TRIGGER_PIN 0
#define IR_PIN 14
// App Layout
#define AC_PWR V4
#define AC_SET_TEMP V5
#define AC_DISP_TEMP V21
#define AC_SET_FAN V6
#define AC_DISP_FAN V22
#define AC_SELECT V0
#define AC_UNITS V23
#define AC_TURBO V7
#define AC_LIGHT V8
#define AC_SWING_V V9
#define AC_SWING_H V10
#define AC_MODE V3

//Config
int timeout = 120;
char blynk_token[34] = "";
String hostname = "GeerAC " + String(ESP.getChipId());
char *update_path = "/firmware";
char *update_username = "admin";
char *update_password = "admin";

//Flags
bool webportalrunning = false;
int start = millis();

// Global Variables
IRac ac(IR_PIN);

void load(WiFiManager &wifiManager)
{
  //clean FS, for testing
  //  SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");
  if (SPIFFS.begin())
  {
    if (SPIFFS.exists("/config.json"))
    {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile)
      {
        Serial.println("opened config file...");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject &json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial); // debug output to console
        if (json.success())
        {
          Serial.println("\nparsed json");

          //  Custom Data to be stored in flash
          strcpy(blynk_token, json["blynk_token"]);
          hostname = json["hostname"].asString();
          strcpy(update_username, json["update_username"]);
          strcpy(update_password, json["update_password"]);

          if (json["Networks"].is<JsonArray>())
          {
            for (int i = 0; i < json["Networks"].size(); i++)
            {
              auto obj = json["Networks"][i];

              // add existing networks and credentials
              wifiManager.addAP(obj["SSID"], obj["Password"]);
              wifiManager.addAP(obj["Sander"], obj["9780978978"]);
            }
          }
        }
        else
        {
          Serial.println("failed to load json config");
          return;
        }
      }
    }
    SPIFFS.end();
  }
  else
  {
    Serial.println("failed to mount FS -> format");
    SPIFFS.format();
  }
}

void save(WiFiManager &wifiManager)
{
  //save the custom parameters to FS
  if (SPIFFS.begin())
  {
    Serial.println("saving config...");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &json = jsonBuffer.createObject();
    JsonArray &networks = json.createNestedArray("Networks");

    // encode known networks to JSON
    for (int i = 0; auto ap = wifiManager.getAP(i); i++)
    {
      JsonObject &obj = jsonBuffer.createObject();
      obj["SSID"] = ap->ssid;
      obj["Password"] = ap->pass;
      networks.add(obj);
    }
    json["blynk_token"] = blynk_token;
    json["hostname"] = hostname.c_str();
    json["update_username"] = update_username;
    json["update_password"] = update_password;
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
      return;
    }

    json.printTo(Serial);
    Serial.println(); // debug output to console
    json.printTo(configFile);
    configFile.close();
    SPIFFS.end();
  }
}

void setup_ac()
{
  // Set up what we want to send.
  // See state_t, opmode_t, fanspeed_t, swingv_t, & swingh_t in IRsend.h for
  // all the various options.
  ac.next.protocol = decode_type_t::COOLIX;      // Set a protocol to use.
  ac.next.model = 1;                             // Some A/Cs have different models. Try just the first.
  ac.next.mode = stdAc::opmode_t::kCool;         // Run in cool mode initially.
  ac.next.celsius = true;                        // Use Celsius for temp units. False = Fahrenheit
  ac.next.degrees = 25;                          // 25 degrees.
  ac.next.fanspeed = stdAc::fanspeed_t::kMedium; // Start the fan at medium.
  ac.next.swingv = stdAc::swingv_t::kOff;        // Don't swing the fan up or down.
  ac.next.swingh = stdAc::swingh_t::kOff;        // Don't swing the fan left or right.
  ac.next.light = false;                         // Turn off any LED/Lights/Display that we can.
  ac.next.beep = false;                          // Turn off any beep from the A/C if we can.
  ac.next.econo = false;                         // Turn off any economy modes if we can.
  ac.next.filter = false;                        // Turn off any Ion/Mold/Health filters if we can.
  ac.next.turbo = false;                         // Don't use any turbo/powerful/etc modes.
  ac.next.quiet = false;                         // Don't use any quiet/silent/etc modes.
  ac.next.sleep = -1;                            // Don't set any sleep time or modes.
  ac.next.clean = false;                         // Turn off any Cleaning options if we can.
  ac.next.clock = -1;                            // Don't set any current time if we can avoid it.
  ac.next.power = false;                         // Initially start with the unit off.
}
//flag for saving data
bool shouldSaveConfig = false;
//callback notifying us of the need to save config
void saveConfigCallback()
{
  shouldSaveConfig = true;
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  //WiFiManager

  //reset settings - for testing
  //wifiManager.resetSettings();

  // load known access points
  load(wifiManager);
  wifiManager.setHostname(hostname.c_str());

  WiFiManagerParameter custom_text0("<p>Select your wifi network and type in your password, if you do not see your wifi then scroll down to the bottom and press scan to check again.</p>");
  WiFiManagerParameter custom_text1("<h1>Hostname</h1>");
  WiFiManagerParameter custom_text2("<p>Enter a name for this device which will be used for the hostname on your network.</p>");
  WiFiManagerParameter custom_hostname("Hostname", "Hostname", hostname.c_str(), 63);

  WiFiManagerParameter custom_text4("<h1>Blynk Settings</h1>");
  WiFiManagerParameter custom_blynk_token("blynk", "Blynk Token", blynk_token, 34);
  WiFiManagerParameter custom_text5("<h1>Web Updater</h1>");
  WiFiManagerParameter custom_text6("<p>The web updater allows you to update the firmware of the device via a web browser by going to its ip address or hostname /firmware ex. 192.168.0.5/firmware you can change the update path below. The update page is protected so enter a username and password you would like to use to access it. </p>");
  WiFiManagerParameter custom_update_username("user", "Username For Web Updater", update_username, 40);
  WiFiManagerParameter custom_update_password("password", "Password For Web Updater", update_password, 40);
  WiFiManagerParameter custom_text8("");


  wifiManager.setCustomHeadElement("<style>.c{text-align: center;} div,input{padding:5px;font-size:1em;} input{width:95%;} body{text-align: center;font-family:oswald;} button{border:0;background-color:#313131;color:white;line-height:2.4rem;font-size:1.2rem;text-transform: uppercase;width:100%;font-family:oswald;} .q{float: right;width: 65px;text-align: right;} body{background-color: #575757;}h1 {color: white; font-family: oswald;}p {color: white; font-family: open+sans;}a {color: #78C5EF; text-align: center;line-height:2.4rem;font-size:1.2rem;font-family:oswald;}</style>");
  wifiManager.addParameter(&custom_text0);
  wifiManager.addParameter(&custom_text1);
  wifiManager.addParameter(&custom_text2);
  wifiManager.addParameter(&custom_hostname);

  wifiManager.addParameter(&custom_text4);
  wifiManager.addParameter(&custom_blynk_token);
  wifiManager.addParameter(&custom_text5);
  wifiManager.addParameter(&custom_text6);
  wifiManager.addParameter(&custom_update_username);
  wifiManager.addParameter(&custom_update_password);
  wifiManager.addParameter(&custom_text8);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setConfigPortalTimeout(180);
  //fetches ssid and pass and tries to connect85d4f18fda6946e8a4fafca8d185bf60
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(hostname.c_str()))
  {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }
  // Read the updated Parameters
  strcpy(blynk_token, custom_blynk_token.getValue());
  hostname = (String)custom_hostname.getValue();
  strcpy(update_username, custom_update_username.getValue());
  strcpy(update_password, custom_update_password.getValue());

  // save known access points
  if (shouldSaveConfig)
    save(wifiManager);

  //if you get here you have connected to the WiFi
  Serial.println();
  Serial.print("connected to: ");
  Serial.println(WiFi.SSID());
  Serial.print("local ip: ");
  Serial.println(WiFi.localIP());

  //  Arduino OTA
  ArduinoOTA.setHostname(hostname.c_str());
  ArduinoOTA.setPassword(update_password);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
    {
      Serial.println("Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR)
    {
      Serial.println("Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      Serial.println("Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      Serial.println("Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      Serial.println("End Failed");
    }
  });

  MDNS.begin(hostname.c_str());

  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);
  // Blynk
  Blynk.config(blynk_token);

  // AC Config
  setup_ac();
}

void loop()
{
  if (webportalrunning)
  {
    wifiManager.process();
    if (millis() - start > timeout * 1000)
    {
      Serial.println("webportaltimeout");
      webportalrunning = false;
      wifiManager.stopWebPortal(); // auto off
    }
  }

  // is configuration portal requested?
  if (digitalRead(TRIGGER_PIN) == LOW && !webportalrunning)
  {
    wifiManager.startWebPortal();
    webportalrunning = true;
    start = millis();
  }
  // Arduino OTA
  ArduinoOTA.handle();
  // HTTP Update 
  httpServer.handleClient();
  MDNS.update();
  // BLynk
  Blynk.run();
}
BLYNK_CONNECTED()
{
  Blynk.syncVirtual(AC_SET_FAN, AC_SET_TEMP, AC_SELECT, AC_UNITS, AC_MODE);
}

BLYNK_WRITE(AC_MODE)
{
  switch (param.asInt())
  {
  case 1: // AUTO
    ac.next.mode = stdAc::opmode_t::kAuto;
    break;

  case 2: //COOL
    ac.next.mode = stdAc::opmode_t::kCool;
    break;

  case 3: //HEAT
    ac.next.mode = stdAc::opmode_t::kHeat;
    break;

  case 4: //DRY
    ac.next.mode = stdAc::opmode_t::kDry;
    break;

  case 5: //FAN
    ac.next.mode = stdAc::opmode_t::kFan;
    break;

  default:
    break;
  }
  ac.sendAc();
}

BLYNK_WRITE(AC_PWR)
{
  int data = param.asInt();
  ac.next.power = data;
  ac.sendAc();
  Serial.printf("AC Turned ON! %d", data);
}

BLYNK_WRITE(AC_SET_TEMP)
{
  int data = param.asInt();
  if (data)
  {
    ac.next.degrees = data;
    ac.sendAc();
    Blynk.virtualWrite(AC_DISP_TEMP, data);
  }
}

BLYNK_WRITE(AC_SET_FAN)
{
  int data = param.asInt();
  switch (data)
  {
  case 0:
    ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
    Blynk.virtualWrite(AC_DISP_FAN, "AUTO");
    break;
  case 1:
    ac.next.fanspeed = stdAc::fanspeed_t::kMin;
    Blynk.virtualWrite(AC_DISP_FAN, "LOW");
    break;
  case 2:
    ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
    Blynk.virtualWrite(AC_DISP_FAN, "MEDIUM");
    break;
  case 3:
    ac.next.fanspeed = stdAc::fanspeed_t::kMax;
    Blynk.virtualWrite(AC_DISP_FAN, "HIGH");
    break;
    ac.sendAc();
  }
}

BLYNK_WRITE(AC_SELECT)
{
  switch (param.asInt())
  {
  case 1: // Daikin
    ac.next.protocol = decode_type_t::DAIKIN;
    break;
  case 2: // Carrier
    ac.next.protocol = decode_type_t::COOLIX;
    break;
  case 3: // LG
    ac.next.protocol = decode_type_t::LG;
    break;
  }
  ac.sendAc();
}

BLYNK_WRITE(AC_UNITS)
{
  ac.next.celsius = param.asInt();
  ac.sendAc();
}

BLYNK_WRITE(AC_TURBO)
{
  ac.next.turbo = param.asInt();
  ac.sendAc();
}
BLYNK_WRITE(AC_LIGHT)
{
  ac.next.light = param.asInt();
  ac.sendAc();
}
BLYNK_WRITE(AC_SWING_H)
{
  if (param.asInt())
  {
  }
  else
  {
    ac.next.swingh = stdAc::swingh_t::kOff; // Don't swing the fan left or right.
  }
}

BLYNK_WRITE(AC_SWING_V)
{
  if (param.asInt())
  {
  }
  else
  {
    ac.next.swingv = stdAc::swingv_t::kOff; // Don't swing the fan up or down.
  }
}
