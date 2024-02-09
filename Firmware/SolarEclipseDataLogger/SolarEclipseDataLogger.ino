#define TIME_AND_DATE_BUTTON 2 // pulled low by default
#define LAT_LONG_BUTTON 1 // pulled low by default
#define DATA_BUTTON 0 // pulled high by default
#define LIGHT_SENSOR A0
#define SD_CS 10

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <Fonts/FreeMono12pt7b.h>
#include <SD.h>
#include "RTClib.h"
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>


const char* ssid = "SSID";
const char* password = "PASSWORD";
float latitude = 44.000, longitude = -73.000; // Change the latitude and longitude values to match the location where the elcipse will be viewed
unsigned long lastTimeDataRecorded = 0; //lastTimeTimeAndDateUpdated = 0;

// light sensor variables
float ambientLight = 0.0, prevAmbientLight = 0.0;

// temp, humidity, and pressure variables
Adafruit_BME280 bme;
float temperature = 0.0, prevTemperature = 0.0;

// TFT display and button variables
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
const int displayWidth = 135;
const int displayHeight = 240;
unsigned long lastTimeTimeAndDateButtonWasPressed = 0, lastTimeLatLongButtonWasPressed = 0, lastTimeDataButtonWasPressed = 0;
bool timeAndDateButtonWasPressed = false, latLongButtonWasPressed = false, dataButtonWasPressed = true, clearDisplay = false;

// SD card variables
File dataFile;
String fileName;

// Real Time Clock variables
RTC_PCF8523 rtc;
DateTime now;
int prevSecond = -1;

// NTP variables
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
//const long utcOffsetInSeconds = 3600 * -5;  // Example: UTC-5 hours


// Interrupt Service Routines
void IRAM_ATTR timeAndDateButton(){
  if(millis() - lastTimeTimeAndDateButtonWasPressed > 250){
    lastTimeTimeAndDateButtonWasPressed = millis();
    timeAndDateButtonWasPressed = true;
    latLongButtonWasPressed = false;
    dataButtonWasPressed = false;
  }
}
void IRAM_ATTR latLongButton(){
  if(millis() - lastTimeLatLongButtonWasPressed > 250){
    lastTimeLatLongButtonWasPressed = millis();
    timeAndDateButtonWasPressed = false;
    latLongButtonWasPressed = true;
    dataButtonWasPressed = false;
    clearDisplay = true;
  }
}
void IRAM_ATTR dataButton(){
  if(millis() - lastTimeDataButtonWasPressed > 250){
    lastTimeDataButtonWasPressed = millis();
    timeAndDateButtonWasPressed = false;
    latLongButtonWasPressed = false;
    dataButtonWasPressed = true;
    clearDisplay = true;
  }
}

void setup(){
  Serial.begin(115200);

  // Initialize TFT
  pinMode(TFT_BACKLITE, OUTPUT);
  digitalWrite(TFT_BACKLITE, HIGH);
  pinMode(TFT_I2C_POWER,OUTPUT);
  digitalWrite(TFT_I2C_POWER, HIGH);
  delay(10);
  tft.init(displayWidth, displayHeight); // Init ST7789 240x135
  tft.setRotation(1); tft.setFont(&FreeMono12pt7b); tft.setCursor(30, 60);
  drawEclipse();
  //tft.fillScreen(ST77XX_BLACK);
  tft.println("Solar Eclipse\n   Data Logger "); delay(5000);

  // Initialize up buttons
  attachInterrupt(TIME_AND_DATE_BUTTON, timeAndDateButton, RISING);
  attachInterrupt(LAT_LONG_BUTTON, latLongButton, RISING);
  attachInterrupt(DATA_BUTTON, dataButton, FALLING);

  // Initialize light sensor and temp, humidity, and pressure sensor
  pinMode(LIGHT_SENSOR, INPUT);
  bool status = bme.begin();

  // Initialize SD card reader
  Serial.print("Initializing SD card reader");
  tft.fillScreen(ST77XX_BLACK); tft.setCursor(0, 14); tft.print("Initializing SD card reader"); delay(1000);
  while(!SD.begin(SD_CS)){
    Serial.print(".");
    tft.print(".");
    delay(100);
  }

  // setup real time clock
  Serial.print("Initializing real time clock");
  tft.fillScreen(ST77XX_BLACK); tft.setCursor(0, 14); tft.print("Initializing real time clock"); delay(1000);
  while(!rtc.begin()) {
    Serial.print(".");
    tft.print(".");
    Serial.flush();
    delay(100);
  }

  // Initialize WiFi
  Serial.print("Connecting to WiFi");
  tft.fillScreen(ST77XX_BLACK); tft.setCursor(0, 14); tft.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiTimeout) < 10000) {
    Serial.print(".");
    tft.print(".");
    delay(1000);
  }
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("WiFi connection timed out");
    tft.fillScreen(ST77XX_BLACK); tft.setCursor(0, 14); tft.println("WiFi connection timed out");
    delay(2000);
  }
  else{
    tft.fillScreen(ST77XX_BLACK); tft.setCursor(0, 14); tft.println("Connected to WiFi"); delay(1000);
    // Initialize NTP
    timeClient.begin();
    //timeClient.setTimeOffset(utcOffsetInSeconds); // Set the time offset based on the local time zone
    // Wait for the time to be set from the NTP server
    while (!timeClient.update()) {
      timeClient.forceUpdate();
      delay(100);
    }
    // Set the RTC time based on the time received from the NTP server
    rtc.adjust(DateTime(timeClient.getEpochTime()));
    Serial.println("Time set from NTP server");
    tft.println("Time set from NTP server"); delay(1000);
    tft.fillScreen(ST77XX_BLACK);
  }

  // Write first line of headers for .csv file
  now = rtc.now();
  fileName = "/"+now.timestamp(DateTime::TIMESTAMP_DATE)+".csv";
  if(!SD.exists(fileName)){
    dataFile = SD.open(fileName, FILE_WRITE);
    if(dataFile){
      dataFile.print("latitude");
      dataFile.print(",");
      dataFile.print("longitude");
      dataFile.print(",");
      dataFile.print("timestamp UTC");
      dataFile.print(",");
      dataFile.print("light Value");
      dataFile.print(",");
      dataFile.println("temperature °C");
      dataFile.close();
    }
    else{
      Serial.println("error opening " + fileName);
      tft.fillScreen(ST77XX_BLACK); tft.setCursor(0, 14); tft.println("error opening " + fileName); delay(1000);
    }
  }
}

void loop(){
  now = rtc.now();
  ambientLight = prevAmbientLight*0.8 + analogRead(LIGHT_SENSOR)*0.2;
  prevAmbientLight = ambientLight;
  temperature = prevTemperature*0.8 + bme.readTemperature()*0.2;
  prevTemperature = temperature;
  if(millis() - lastTimeDataRecorded >= 1000){
    lastTimeDataRecorded = millis();
    printDataToSerial();
    writeDataToFile();
    if(dataButtonWasPressed){
      tft.fillRect(0,22,displayHeight,22,ST77XX_BLACK);
      tft.fillRect(0,69,displayHeight,23,ST77XX_BLACK);
      printDataToDisplay();
    }
  }
  if(dataButtonWasPressed){
    if(clearDisplay){
      clearDisplay = false;
      tft.fillScreen(ST77XX_BLACK);
      printDataToDisplay();
    }
  }
  if(timeAndDateButtonWasPressed){
    if(now.second() != prevSecond){
      prevSecond = now.second();
      tft.fillScreen(ST77XX_BLACK);
      tft.setCursor(0, 14); tft.setTextColor(ST77XX_WHITE);
      tft.println(now.timestamp(DateTime::TIMESTAMP_DATE));
      tft.print(now.timestamp(DateTime::TIMESTAMP_TIME));
      tft.println(" UTC");
    }
  }
  if(latLongButtonWasPressed){
    if(clearDisplay){
      clearDisplay = false;
      tft.fillScreen(ST77XX_BLACK);
    }
    tft.setCursor(0, 14); tft.setTextColor(ST77XX_CYAN);
    tft.println("Latitude");
    tft.println(latitude);
    tft.println("Longitude");
    tft.println(longitude);
  }
}

void printDataToSerial(){
  Serial.print(latitude);
  Serial.print(",");
  Serial.print(longitude);
  Serial.print(",");
  Serial.print(now.timestamp());
  Serial.print(",");
  Serial.print(ambientLight);
  Serial.print(",");
  Serial.println(temperature);
}

void printDataToDisplay(){
  tft.setCursor(0, 14); tft.setTextColor(ST77XX_ORANGE);
  tft.println("Light value");
  tft.println(ambientLight);
  tft.println("Temperature");
  tft.print(temperature);
  tft.write(0xF8);  // Print the degrees symbol
  tft.println("C");
}

void writeDataToFile(){
  dataFile = SD.open(fileName, FILE_APPEND);
  if(dataFile){
    dataFile.print(String(latitude));
    dataFile.print(",");
    dataFile.print(String(longitude));
    dataFile.print(",");
    dataFile.print(now.timestamp());
    dataFile.print(",");
    dataFile.print(String(ambientLight));
    dataFile.print(",");
    dataFile.println(String(temperature));
    dataFile.close();
  }
  else{
    Serial.println("error opening " + fileName);
  }
}

void drawEclipse(){
  tft.fillScreen(ST77XX_BLACK);
  tft.fillCircle(displayHeight/2, displayWidth/2, 50, ST77XX_YELLOW);
  for(int xPos=displayHeight; xPos >= displayHeight/2; xPos--){
    tft.fillCircle(xPos, displayWidth/2, 48, ST77XX_BLACK);
    delay(20);
  }
  tft.drawCircle(displayHeight/2, displayWidth/2, 50, ST77XX_YELLOW);
  delay(1000);
}