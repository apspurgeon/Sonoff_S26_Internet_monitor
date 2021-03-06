//
//  Sonoff turns on relay, pings external IP, if successful begins monitoring loop
//  In loop, check external IPs (randomly choose between 2), if ping fails more than x time, then check internal ip
//  if internal IP also fail don't do anything (Wifi or other issue in the house)
//  if interanl works but external doesn't, turn off/on the relay, wait for router restart
//  Begin loop again, if it still doesn't work increase the delay period between pings.
//

#include <ESP8266WiFi.h>
#include <ESP8266Ping.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WiFiMulti.h>
#include <ArduinoOTA.h>

ESP8266WiFiMulti wifiMulti; // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'

#define xstr(s) str(s)
#define str(s) #s
#define BLYNK_PRINT Serial

const char *remote_host = "203.109.191.1";        //External site for pringing (Vodafone DNS)
const char *remote_host2 = "www.google.co.nz";    //2nd External site for pinging
const char *remote_host_internal = "192.168.2.1"; //Router.  If this isn't working keep relay on
bool ping_result;

//Gets SSID/PASSWORD from Platform.ini build flags
#ifndef SSID_NAME
#define SSID_NAME "WIFI_SSID" //Default SSID if not build flag from PlatformIO doesn't work
#endif

#ifndef PASSWORD_NAME
#define PASSWORD_NAME "WIFI_PASSWORD" //Default WiFi Password if not build flag from PlatformIO doesn't work
#endif

#ifndef DBLYNKCERT_NAMEDBLYNKCERT_NAME
#define DBLYNKCERT_NAME "1234567890" //Default BLYNK Cert if not build flag from PlatformIO doesn't work
#endif

const char ssid[] = xstr(SSID_NAME);      //  your network SSID (name)
const char pass[] = xstr(PASSWORD_NAME);  // your network password
const char auth[] = xstr(BLYNKCERT_NAME); // your BLYNK Cert

//Sonof S26 pins
int gpio_13_led = 13;
int gpio_12_relay = 12;

//Configuration variables (all times in ms)
int check_period = 10000;       //Check pin every 60sec
int check_period_start = 10000; //Check pin every 60sec
int fail_retry_period = 5000;   //If it failed, try again in 5sec
int fail_times = 5;             //Try 5 times, and only then turn relay off
int fail_count = 0;             //Try 5 times, and only then turn relay off
int relay_offtime = 5000;       //When rely off, turn off for 5sec
int startup_time = 120000;      //Once restarted, wait period before ping tests
int restart_count = 0;          //How many times has the sonoff restarted the device during fail period
int total_restart_count = 0;    //Total How many times has the sonoff restarted then device
int inc_delay = 2000;           //Additional wait period * number of restarts in this fail period
int total_ping_fails = 0;       //Keep a track of total fails
int total_norestart_localfail = 0;
int fails_without_restart = 0;
int total_fails_without_restart = 0;
int IP1_fails = 0;
int IP2_fails = 0;
int total_IP1_fails = 0;
int total_IP2_fails = 0;
int total_cycles = 0;

//Working variables
int firstping = 0; //Flag for 1st ping
//int firstcount = 1;
long delay_working = 0;

//Declare functions
void firstcheck();
void ping_time();
void fail_check();
void StartOTA();

//BLYNK_WRITE(V1);
WidgetLCD lcd(V1);

BLYNK_WRITE(V10)
{
  if (param.asInt() == 1)
  {
  // Reset ALL counters
  restart_count = 0;
  total_restart_count = 0;
  total_ping_fails = 0;
  total_norestart_localfail = 0;
  total_IP1_fails = 0;
  total_IP2_fails = 0;
  total_cycles = 0;
  IP1_fails = 0;
  IP2_fails = 0;
  }
}

BLYNK_WRITE(V11)
{
  if (param.asInt() == 1)
  {
  // Reset counters
  restart_count = 0;
  IP1_fails = 0;
  IP2_fails = 0;  
  }
}

void setup()
{
  Serial.begin(9600);

  pinMode(gpio_12_relay, OUTPUT);
  digitalWrite(gpio_12_relay, HIGH); //Turn relay ON.  Power ON the devices connected to Sonoff

  pinMode(gpio_13_led, OUTPUT);
  digitalWrite(gpio_13_led, HIGH); //Turn main LED off

  //Blynk setup
  Blynk.begin(auth, ssid, pass);
  lcd.clear();

  Serial.println("Relay ON");
  lcd.print(0, 0, "Relay ON");

  Serial.println();
  Serial.println("Connecting to WiFi");
  lcd.print(0, 1, "Connecting WiFi");

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("WiFi connected with ip ");
  lcd.print(0, 0, "WiFi connected");
  Serial.println(WiFi.localIP());
  delay(2000);

  StartOTA();
}

void loop()
{
  ArduinoOTA.handle();
  //Blynk.run();
  firstcheck();

  //Wait period between pings
  check_period_start = millis();
  while (millis() <= check_period_start + check_period)
  {
    yield();
    Blynk.run();
  }

  Serial.print("Fails without restart");
  Serial.print(fails_without_restart);
  Serial.print("Total fails without restart");
  Serial.print(total_fails_without_restart);  
  Serial.print(",   Total restart count = ");
  Serial.println(total_restart_count);
  Serial.print("Total ping fails = ");
  Serial.println(total_ping_fails);

  //both fail - no restart - wifi fail.  External fails, but not enought to restart
  Blynk.virtualWrite(V2, total_restart_count);      //Number of restarts
  Blynk.virtualWrite(V3, total_fails_without_restart);  //Cycles where Ping fails not enough to restart
  Blynk.virtualWrite(V4, total_norestart_localfail);   //Avoided restart as local also failed (Wifi issue?)
  Blynk.virtualWrite(V5, IP1_fails);
  Blynk.virtualWrite(V6, IP2_fails);
  Blynk.virtualWrite(V7, total_IP1_fails);
  Blynk.virtualWrite(V8, total_IP2_fails);
  Blynk.virtualWrite(V9, total_cycles);

  ping_time();
  fail_check();
  total_cycles++;
}

void fail_check()
{
  if (fail_count >= fail_times)
  {
    //Do a final Ping check against a local IP
    if (Ping.ping(remote_host_internal) == false)
    {
      fail_count = 0;
      Serial.println("");
      Serial.println("");
      Serial.println("*** No Relay restart - Local IP Ping also failed ***");
      Serial.println("");
      lcd.clear();
      lcd.print(0, 0, "local ping failed");
      lcd.print(0, 1, "No relay change");
      total_norestart_localfail++;
      return;
    }

    Serial.println("");
    Serial.println("");
    Serial.println("*** Restart relay ***");
    Serial.println("");
    lcd.clear();
    lcd.print(0, 0, "Restart relay");

    digitalWrite(gpio_12_relay, LOW); //Turn relay OFF.  Power OFF the devices connected to Sonoff
    digitalWrite(gpio_13_led, HIGH);
    Serial.print("Relay OFF,  ");
    lcd.print(0, 1, "Relay OFF,  ");

    //Keep relay off for period
    delay_working = millis();
    while (millis() <= delay_working + relay_offtime)
    {
      yield();
    }

    digitalWrite(gpio_12_relay, HIGH); //Turn relay ON.  Power ON the devices connected to Sonoff
    digitalWrite(gpio_13_led, LOW);
    Serial.println("Relay ON");
    lcd.print(0, 1, "Relay OFF, ON");
    Serial.println("");

    //Wait for Modem/Router to initialise
    Serial.println("Device initialisation wait - start");
    delay_working = millis();
    while (millis() <= delay_working + startup_time)
    {
      yield();
    }
    Serial.println("Device initialisation wait - end.  MODEM/Router should be running");
    lcd.clear();
    lcd.print(0, 0, "Device wait end");
    //Relay turned off, and on.  Waited for initialisation period
    restart_count++;
    total_restart_count++;
    fail_count = 0;
  }
}

void ping_time()
{
  //Ping time

  //Randomise ping to internet
  if (random(1000) <= 500)
  {
    ping_result = Ping.ping(remote_host);
    Serial.print("Ping :");
    lcd.clear();
    lcd.print(0, 0, "Ping: ");

    if (ping_result == false){
      IP1_fails++;
      total_IP1_fails++;
    }
  }
  else
  {
    ping_result = Ping.ping(remote_host2);
    Serial.print("Ping :");
    lcd.clear();
    lcd.print(0, 0, "Ping: ");

    if (ping_result == false){
      IP2_fails++;
      total_IP2_fails++;
    }
  
  }

  if (ping_result == true)
  {
    Serial.println("Success");
    lcd.print(0, 0, "Ping: Success");
    Serial.println("");
    restart_count = 0; //Reset the number of restarts
    fail_count = 0;
  }
  else
  {
    //Ping failed
    //Loop fails until relay restart or success and return
    while (fail_count < fail_times)
    {
      fail_count++;
      total_ping_fails++;
      Serial.print("Fail: ");
      Serial.print(fail_count);
      Serial.print(", ");
      lcd.clear();
      lcd.print(0, 0, "Ping: Fail");
      lcd.print(0, 1, fail_count);

      //wait for a bit
      delay_working = millis();
      while (millis() <= delay_working + fail_retry_period + (inc_delay * restart_count)) //Sonof will increase the wait period between ping tries after each relay reset
      {
        yield();
      }

      //Try again
      //Randomise ping
      if (random(1000) <= 500)
      {
        ping_result = Ping.ping(remote_host);
        
        if (ping_result == false){
        IP1_fails++;
        total_IP1_fails++;
        }   
      }
      else
      {
        ping_result = Ping.ping(remote_host2);
        
        if (ping_result == false){
        IP2_fails++;
        total_IP2_fails++;
        }
      }

      if (ping_result == true)
      {
        Serial.println("Success");
        Serial.println("");
        lcd.clear();
        lcd.print(0, 0, "Ping: success");
        restart_count = 0; //Reset the number of restarts
        fail_count = 0;    //Reset the count
        total_fails_without_restart++;
        return;            //Escape out of Ping function
      }
    }
    //No success, and hit the fail count.  Time to restart relay
    return; //Escape out of Ping function
  }
}

void firstcheck()
{
  if (firstping == 0)
  {
    digitalWrite(gpio_13_led, HIGH); //Main LED off
    Serial.print("Pinging host for first time ");
    Serial.println(remote_host);
    lcd.clear();
    lcd.print(0, 0, "First Ping: ");
    lcd.print(0, 1, remote_host);

    //Randomise ping
    if (random(1000) <= 500)
    {
      ping_result = Ping.ping(remote_host);
    }
    else
    {
      ping_result = Ping.ping(remote_host2);
    }

    if (ping_result == true)
    {
      Serial.println("Success");
      lcd.clear();
      lcd.print(0, 0, "1st Ping:Success");
      Serial.println("");
      digitalWrite(gpio_13_led, LOW); //Main LED on
      firstping = 1;
      return;
    }
    else
    {
      //Randomise ping
      if (random(1000) <= 500)
      {
        ping_result = Ping.ping(remote_host);
      }
      else
      {
        ping_result = Ping.ping(remote_host2);
      }

      while (ping_result == false)
      {
        fail_count++;
        Serial.print("Fail: ");
        Serial.print(fail_count);
        Serial.print(", ");
        lcd.clear();
        lcd.print(0, 0, "Ping: Fail");
        lcd.print(0, 1, fail_count);

        digitalWrite(gpio_13_led, LOW); //Main LED on
        delay_working = millis();
        while (millis() <= delay_working + 200)
        {
          yield();
        }
        digitalWrite(gpio_13_led, HIGH); //Main LED off

        //Randomise ping
        if (random(1000) <= 500)
        {
          ping_result = Ping.ping(remote_host);
        }
        else
        {
          ping_result = Ping.ping(remote_host2);
        }
      }

      Serial.println("Success");
      Serial.println("Sonoff ready to begin monitoring");
      lcd.clear();
      lcd.print(0, 0, "Success");
      lcd.print(0, 1, "Monitoring...");
      Serial.println("");
      digitalWrite(gpio_13_led, LOW); //Main LED on
      firstping = 1;
      fail_count = 0;
      return;
    }
  }
}

void StartOTA()
{
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
  ArduinoOTA.begin();

  Serial.println();
  Serial.println("OTA Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("");
}