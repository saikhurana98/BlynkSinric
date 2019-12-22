//needed for library
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <ESP8266WiFi.h>
#include <Blynk.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

//Pinout
  //Pinout
  //Hardware (Wemos D1 Mini)
  #define rPin 12 //D6
  #define gPin 14 //D5
  #define bPin 13 //D7
  //Hardware (NodeMCU)
  // #define rPin 12 //D6
  // #define gPin 14 //D5
  // #define bPin 04 //D2

  //App Layout
  #define lightSwitch V0
  #define zRGBra V1
  #define rSlider V2
  #define gSlider V3
  #define bSlider V4
  #define bright V5
    
//Parameters
  char auth[] = "53f4b84a31fb44a19941497db01ae5af"; //Auth token from the blynk app
  const char* host = "esp8266-webupdate";
  const char* update_path = "/firmware";
  const char* update_username = "admin";
  const char* update_password = "admin";

//Global Config
  #define BLYNK_PRINT Serial

//initilising
  ESP8266WebServer httpServer(80);  
  ESP8266HTTPUpdateServer httpUpdater;

//Global Variables 
  int _targetR = 0;
  int _targetG = 0;
  int _targetB = 0;
  int bright_rgb = 75;
  int prevR, prevG, prevB;
  int inc = 1;

void setup()
{
  pinMode(rPin,OUTPUT);
  pinMode(gPin,OUTPUT);
  pinMode(bPin,OUTPUT);
  pinMode(headPin,OUTPUT);
  Serial.begin(115200);

  //WiFiManager 
  WiFiManager wifiManager;
  wifiManager.setTimeout(180);
  if(!wifiManager.autoConnect("AutoConnectAP")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //OTA Update Config
  MDNS.begin(host);
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);

  //Blynk 
  Blynk.config(auth);
}

void loop()
{ 
  //OTA Server
  httpServer.handleClient();
  //Blynk
  Blynk.run();
}

BLYNK_CONNECTED() 
{
    Blynk.syncAll();
}

BLYNK_WRITE(zRGBra) 
{ 
  //Storing the values in global variable
  // get a RED channel value
  _targetR = param[0].asInt();
  // get a GREEN channel value
  _targetG = param[1].asInt();
  // get a BLUE channel value
  _targetB = param[2].asInt();

  //Blynk ViretualPin Update
  Blynk.virtualWrite(V2,_targetR);
  Blynk.virtualWrite(V3,_targetG);
  Blynk.virtualWrite(V4,_targetB);

  //Write Funciton Call
  write_rgb(bright_rgb);
}

BLYNK_WRITE(lightSwitch)
{
  int state = param.asInt();
  if (state == 1)
  {
    write_rgb(bright_rgb);
  }
  else
  {
    write_rgb(0);
  }
}

BLYNK_WRITE(rSlider) //Red slider widget
{ 
  _targetR = param.asInt();
  write_rgb(bright_rgb);
}

BLYNK_WRITE(gSlider) //Green slider widget
{ 
  _targetG = param.asInt();
  write_rgb(bright_rgb);
}

BLYNK_WRITE(bSlider) //Blue slider widget
{ 
  _targetB = param.asInt();
  write_rgb(bright_rgb);
}

BLYNK_WRITE(bright) //RGB Brightness slider widget
{ 
  bright_rgb = param.asInt();

  if (bright_rgb != 0){
    Blynk.virtualWrite(lightSwitch,1);
  }else{
    Blynk.virtualWrite(lightSwitch,1);
  }

  write_rgb(bright_rgb);     
}

void write_rgb(int brightness)
{ 
  int currR = _targetR * brightness / 100;
  int currG = _targetG * brightness / 100;
  int currB = _targetB * brightness / 100;

  while(prevR != currR || prevG != currG || prevB != currB) 
  {
      //RED
    if (prevR > currR) {
      prevR-=inc;
    }
    else if (prevR < currR) {
      prevR+=inc;
    }

      //GREEN
    if (prevG > currG) {
      prevG-=inc;
    }
    else if (prevG < currG) {
      prevG+=inc;
    }

      //BLUE
    if (prevB > currB) {
      prevB-=inc;
    }
    else if (prevB < currB) {
      prevB+=inc;
    }
    //Writing Data
    Serial.printf("RED: %d Green: %d BLUE %d \n",  prevR , prevG , prevB);
    analogWrite(rPin, prevR * 4);
    analogWrite(gPin, prevG * 4);
    analogWrite(bPin, prevB * 4);
  }
}