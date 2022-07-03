#include <BlynkSimpleEsp8266.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/********************************************************************/
// Data wire is plugged into pin 2 on the Arduino 
#define ONE_WIRE_BUS 13 
/********************************************************************/
// Setup a oneWire instance to communicate with any OneWire devices  
// (not just Maxim/Dallas temperature ICs) 
OneWire oneWire(ONE_WIRE_BUS); 
/********************************************************************/
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);
/********************************************************************/ 


// Esp8266 pins.
#define ESP8266_GPIO4    4 // Relay control


#define str(s) #s
#define xstr(s) str(s)

ESP8266WiFiMulti wifiMulti; // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'

//Wifi - BLYNK
#ifndef BLYNKCERT_NAME
#define BLYNKCERT_NAME "1234567890" //Default BLYNK Cert if not build flag from PlatformIO doesn't work
#endif

#ifndef SSID_NAME
#define SSID_NAME "WIFI_SSID" //Default SSID if not build flag from PlatformIO doesn't work
#endif

#ifndef PASSWORD_NAME
#define PASSWORD_NAME "WIFI_PASSWORD" //Default WiFi Password if not build flag from PlatformIO doesn't work
#endif

//Gets SSID/PASSWORD from PlatformIO.ini build flags
const char ssid[] = xstr(SSID_NAME);      //  your network SSID (name)
const char pass[] = xstr(PASSWORD_NAME);  // your network password
const char auth[] = xstr(BLYNKCERT_NAME); // your BLYNK Cert

HTTPClient http;

//vars
float temp;
int trigger_temp = 23;
String fan_state = "OFF";
int ON_time_mins = 0;    //Mins fan is on
int OFF_time_mins = 0;    //Mins fan is on
unsigned long ONOFF_time;  //for millis


//BLYNK Definition to capture virtual pin to change trigger temp
BLYNK_WRITE(V1)
{
  trigger_temp = param.asInt();
}

WidgetLED led1(V2);

void ConnectToAP();




void setup()
{
  Serial.begin(9600);
  sensors.begin(); 

  //Connect to Wifi
  ConnectToAP();

  pinMode(ESP8266_GPIO4, OUTPUT);

  Blynk.config(auth);

  Serial.print("Startup: ");

  //ON-OFF at start up
      digitalWrite(ESP8266_GPIO4, HIGH); //Turn relay ON.  Power ON the devices connected to Sonoff
  
  delay(2500);
      digitalWrite(ESP8266_GPIO4, LOW); //Turn relay OFF.
      ONOFF_time = (millis() / 1000);
      fan_state = "OFF";
 
  delay (2500);

      Serial.println("Startup test complete");
      Serial.println();

  //Display something on the App
  Blynk.virtualWrite(V1, trigger_temp);      

      
}



void loop(){

  Blynk.run();

  sensors.requestTemperatures(); // Send the command to get temperature readings 
  temp = sensors.getTempCByIndex(0); 

  Serial.print("Fan: ");
  Serial.print(fan_state);
  Serial.print(",  Trigger temp is: "); 
  Serial.print(trigger_temp);
  Serial.print(",  Boat temp is: "); 
  Serial.println (temp);

  Serial.print("ON time (mins): ");
  Serial.print  (ON_time_mins);
  Serial.print(",  OFF time (mins): "); 
  Serial.println  (OFF_time_mins);
  Serial.println();

  Blynk.virtualWrite(V0, temp);

  //Above trigger temp  
  if (temp > trigger_temp){
      digitalWrite(ESP8266_GPIO4, HIGH); //Turn relay ON.  Power ON the devices connected to Sonoff

      if (fan_state == "OFF"){
        ONOFF_time = (millis() / 1000);
      }

      fan_state = "ON";
      ON_time_mins = ((millis() / 1000) - ONOFF_time) / 60;

      Blynk.virtualWrite(V3, ON_time_mins);
      Blynk.virtualWrite(V4, OFF_time_mins);
      Blynk.virtualWrite(V1, trigger_temp);  
      led1.on();
      }

  //turn off when 1 degree below trigger temp
  if (temp < (trigger_temp - 1)){
      digitalWrite(ESP8266_GPIO4, LOW); //Turn relay OFF.

      if (fan_state == "ON"){
        ONOFF_time = (millis() / 1000);
      }
      fan_state = "OFF";
      OFF_time_mins = ((millis() / 1000) - ONOFF_time) / 60;
      
      Blynk.virtualWrite(V3, ON_time_mins);
      Blynk.virtualWrite(V4, OFF_time_mins);
      Blynk.virtualWrite(V1, trigger_temp);  
      led1.off();
      }

  delay (2000);

}


//Connect to access point
void ConnectToAP()
{
  Serial.println("Attempting to Connect");
  randomSeed(analogRead(6));
  
  for (int z = 0; z < 5; z++)
  {
    delay(1000);
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);
    
    for (int x = 0; x < 5; x++)
    {
      delay(1000);
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.print("WiFi connected in ");
        Serial.print(x);
        Serial.println(" seconds");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.println();
        return;
      }
    }
  }
  return;   //failed
}