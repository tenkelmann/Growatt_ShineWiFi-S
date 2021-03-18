#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <stdio.h>
#include <stdlib.h>

#include "Growatt.h"
#include "index.h"
#include "credentials.h"

#define PROJECT_NAME "ShineWiFi-S Modbus"

#define DEBUG
#ifdef DEBUG
 #define DEBUG_PRINT(x)  Serial.println(x)
#else
 #define DEBUG_PRINT(x)
#endif

#define LED_GN 0  // GPIO0
#define LED_RT 2  // GPIO2
#define LED_BL 16 // GPIO16

#define BUTTON 0 // GPIO0

#ifndef MQTT_MAX_PACKET_SIZE
  #define MQTT_MAX_PACKET_SIZE 512
#endif

#define HOSTNAME          "Growatt"

const char* update_path = "/firmware";

WiFiClient   espClient;
PubSubClient MqttClient(espClient);
Growatt      Inverter;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

char MqttPayload[512] = "{\"Status\": \"Disconnected\" }";
uint16_t u16PacketCnt = 0;

long Timer1s = 0;
long Timer10s = 0;

void CreateJson(char *Buffer)
{
  Buffer[0] = 0; // Terminate first byte
  
  #ifndef SIMULATE
    sprintf(Buffer, "{\r\n");
    switch( Inverter.GetStatus() )
    {
      case GwStatusWaiting:
        sprintf(Buffer, "%s  \"Status\": \"Waiting\",\r\n", Buffer);
        break;
      case GwStatusNormal: 
        sprintf(Buffer, "%s  \"Status\": \"Normal\",\r\n", Buffer);
        break;
      case GwStatusFault:
        sprintf(Buffer, "%s  \"Status\": \"Fault\",\r\n", Buffer);
        break;
    }
    sprintf(Buffer, "%s  \"DcVoltage\": %.1f,\r\n",     Buffer, Inverter.GetDcVoltage());
    sprintf(Buffer, "%s  \"AcFreq\": %.3f,\r\n",        Buffer, Inverter.GetAcFrequency());
    sprintf(Buffer, "%s  \"AcVoltage\": %.1f,\r\n",     Buffer, Inverter.GetAcVoltage());
    sprintf(Buffer, "%s  \"AcPower\": %.1f,\r\n",       Buffer, Inverter.GetAcPower());
    sprintf(Buffer, "%s  \"EnergyToday\": %.1f,\r\n",   Buffer, Inverter.GetEnergyToday());
    sprintf(Buffer, "%s  \"EnergyTotal\": %.1f,\r\n",   Buffer, Inverter.GetEnergyTotal());
    sprintf(Buffer, "%s  \"OperatingTime\": %u,\r\n",   Buffer, Inverter.GetOperatingTime());
    sprintf(Buffer, "%s  \"Temperature\": %.1f,\r\n",    Buffer, Inverter.GetInverterTemperature());
    sprintf(Buffer, "%s  \"Cnt\": %u\r\n",              Buffer, u16PacketCnt);
    sprintf(Buffer, "%s}\r\n", Buffer);
  #else
    #warning simulating
    sprintf(Buffer, "{\r\n");
    sprintf(Buffer, "%s  \"Status\": \"Normal\",\r\n",    Buffer);
    sprintf(Buffer, "%s  \"DcVoltage\": 70.5,\r\n",       Buffer);
    sprintf(Buffer, "%s  \"AcFreq\": 50.00,\r\n",         Buffer);
    sprintf(Buffer, "%s  \"AcVoltage\": 230.0,\r\n",      Buffer);
    sprintf(Buffer, "%s  \"AcPower\": 0.00,\r\n",         Buffer);
    sprintf(Buffer, "%s  \"EnergyToday\": 0.3,\r\n",      Buffer);
    sprintf(Buffer, "%s  \"EnergyTotal\": 49.1,\r\n",     Buffer);
    sprintf(Buffer, "%s  \"OperatingTime\": 123456,\r\n", Buffer);
    sprintf(Buffer, "%s  \"Temperature\": 21.12,\r\n",     Buffer);
    sprintf(Buffer, "%s  \"Cnt\": %u\r\n",                Buffer, u16PacketCnt);
    sprintf(Buffer, "%s}", Buffer);
  #endif 
}

void SendJsonSite(void)
{
  httpServer.send(200, "application/json", MqttPayload);
}

void MainPage(void)
{
  httpServer.send(200, "text/html", MAIN_page);
}

// -------------------------------------------------------
// Check the WiFi status and reconnect if necessary
// -------------------------------------------------------
void WiFi_Reconnect()
{
  if( WiFi.status() != WL_CONNECTED )
  {
    digitalWrite(LED_GN, 0);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(200);
      DEBUG_PRINT("x");
      digitalWrite(LED_RT, !digitalRead(LED_RT)); // toggle red led on WiFi (re)connect
    }

    DEBUG_PRINT("local IP:");
    DEBUG_PRINT(WiFi.localIP());
    DEBUG_PRINT("Hostname: ");
    DEBUG_PRINT(HOSTNAME);

    MqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    
    digitalWrite(LED_RT, 0);
  }
}

// -------------------------------------------------------
// Check the Mqtt status and reconnect if necessary
// -------------------------------------------------------
void MqttReconnect() 
{
  // Loop until we're reconnected
  while (!MqttClient.connected()) 
  {
    if( WiFi.status() != WL_CONNECTED )
      break;
    
    Serial.print("Attempting MQTT connection...");
    
    // Attempt to connect with last will
    if (MqttClient.connect("Growatt", MQTT_USER, MQTT_PASS, "LS111/Solar/Growatt1kWp", 1, 1, "{\"Status\": \"Disconnected\" }")) {
      Serial.println("connected");
    } 
    else
    {
      Serial.print("failed, rc=");
      Serial.print(MqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


// -------------------------------------------------------
// Will be executed once after power on
// -------------------------------------------------------
void setup()
{
  pinMode(LED_GN, OUTPUT);
  pinMode(LED_RT, OUTPUT);
  pinMode(LED_BL, OUTPUT);

  httpServer.on("/status", SendJsonSite);
  httpServer.on("/", MainPage);

  Serial.begin(9600); // Baudrate of Growatt

  WiFi.hostname(HOSTNAME);
  WiFi_Reconnect();
  Inverter.begin(Serial);

  httpUpdater.setup(&httpServer, update_path, UPDATE_USER, UPDATE_PASS);
  httpServer.begin();
}

void loop()
{

  long now = millis();
  
  WiFi_Reconnect();
  MqttReconnect();
  
  httpServer.handleClient();
  
  MqttClient.loop();

  // Toggle green LED with 1 Hz (alive)
  // ------------------------------------------------------------
  if (now - Timer1s > 500)
  {
    if( WiFi.status() == WL_CONNECTED )
      digitalWrite(LED_GN, !digitalRead(LED_GN));
    else
      digitalWrite(LED_GN, 0);
    
    Timer1s = now;
  }


    // Read Inverter every 10 s
  // ------------------------------------------------------------
  if (now - Timer10s > 10000)
  {
    if( MqttClient.connected() && (WiFi.status() == WL_CONNECTED) )
    {
      #ifndef SIMULATE
        if( Inverter.UpdateData() ) // get new data from inverter
      #else
        if(1)
      #endif
        {
          u16PacketCnt++;
          CreateJson(MqttPayload);
        
          MqttClient.publish("LS111/Solar/Growatt1kWp", MqttPayload, true);

          digitalWrite(LED_BL, 0); // clear blue led if everything is ok
        }
        else
        {
          sprintf(MqttPayload, "{\"Status\": \"Disconnected\" }");
          MqttClient.publish("LS111/Solar/Growatt1kWp", MqttPayload, true);
          digitalWrite(LED_BL, 1); // set blue led in case of error
        }
    }

    Timer10s = now;
  }
}
