#include <SPI.h>
#include <WiFi.h>
#include "time.h"

#define PIN_LED 26
#define PIN_DIN 13
#define PIN_CLK 14
#define PIN_OE 27

const char *ssid = "***REMOVED***";
const char *wifipw = "***REMOVED***";

struct tm timeInfo;
time_t prevTime = 0;

String stringToDisplay = "000000";
unsigned int symbolArray[10] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512}; // 0 to 9

byte digitPins[6][10] = {
    {130, 121, 122, 123, 124, 125, 126, 127, 128, 129}, // sec  1
    {120, 111, 112, 113, 114, 115, 116, 117, 118, 119}, // sec  10
    {110, 101, 102, 103, 104, 105, 106, 107, 108, 109}, // min   1
    {30, 21, 22, 23, 24, 25, 26, 27, 28, 29},           // min  10
    {20, 11, 12, 13, 14, 15, 16, 17, 18, 19},           // hour  1
    {10, 1, 2, 3, 4, 5, 6, 7, 8, 9}                     // hour 10
};

void blinkError()
{
  for (int i = 0; i < 5; i++)
  {
    digitalWrite(PIN_LED, HIGH);
    delay(300);
    digitalWrite(PIN_LED, LOW);
    delay(300);
  }
}

void setTimezone(String timezone)
{
  Serial.printf("  Setting Timezone to %s\n", timezone.c_str());
  setenv("TZ", timezone.c_str(), 1); //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
}

void initTime(String timezone)
{
  Serial.println("Setting up time");
  configTime(0, 0, "pool.ntp.org"); // First connect to NTP server, with 0 TZ offset
  if (!getLocalTime(&timeInfo))
  {
    Serial.println("  Failed to obtain time");
    blinkError();
    return;
  }
  Serial.println("  Got the time from NTP");
  // Now we can set the real timezone
  setTimezone(timezone);
}

void printLocalTime()
{
  if (!getLocalTime(&timeInfo))
  {
    Serial.println("Failed to obtain time 1");
    blinkError();
    return;
  }
  Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
}

void startWifi()
{
  WiFi.begin(ssid, wifipw);
  Serial.println("Connecting Wifi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    digitalWrite(PIN_LED, HIGH);
    delay(100);
    digitalWrite(PIN_LED, LOW);
    delay(500);
  }
  Serial.print("Wifi RSSI=");
  Serial.println(WiFi.RSSI());
}

void displayDigits()
{
  digitalWrite(PIN_OE, LOW); // allow data input (Transparent mode)
  unsigned long var32 = 0;

  long digits = stringToDisplay.toInt();

  //-------- REG 1 -----------------------------------------------
  var32 = 0;

  var32 |= (unsigned long)(symbolArray[digits % 10]) << 20; // s2
  digits /= 10;

  var32 |= (unsigned long)(symbolArray[digits % 10]) << 10; // s1
  digits /= 10;

  var32 |= (unsigned long)(symbolArray[digits % 10]); // m2
  digits /= 10;

  SPI.transfer(var32 >> 24);
  SPI.transfer(var32 >> 16);
  SPI.transfer(var32 >> 8);
  SPI.transfer(var32);

  //-------- REG 0 -----------------------------------------------
  var32 = 0;

  var32 |= (unsigned long)(symbolArray[digits % 10]) << 20; // m1
  digits = digits / 10;

  var32 |= (unsigned long)(symbolArray[digits % 10]) << 10; // h2
  digits = digits / 10;

  var32 |= (unsigned long)symbolArray[digits % 10]; // h1
  digits = digits / 10;

  SPI.transfer(var32 >> 24);
  SPI.transfer(var32 >> 16);
  SPI.transfer(var32 >> 8);
  SPI.transfer(var32);

  digitalWrite(PIN_OE, HIGH); // latching data
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_OE, OUTPUT);

  Serial.begin(115200);
  Serial.setDebugOutput(true);

  SPI.begin();
  SPI.setDataMode(SPI_MODE2);
  SPI.setClockDivider(SPI_CLOCK_DIV8);

  startWifi();

  initTime("CET-1CEST,M3.5.0,M10.5.0/3");
}

void loop()
{
  if (!getLocalTime(&timeInfo))
  {
    Serial.println("Failed to obtain time");
    blinkError();
    return;
  }
  time_t nowTime = mktime(&timeInfo);
  if (nowTime != prevTime)
  { // Update time every second
    prevTime = nowTime;
    if ((timeInfo.tm_sec >= 50) && (timeInfo.tm_sec < 55))
    {
      displayDate();
    }
    else
    {
      displayTime();
    }
  }
  delay(200);
}
