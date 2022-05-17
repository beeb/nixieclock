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

long digits = 0; // number to display, negative means only 4 digits are shown (second positions are blank)

unsigned int symbolArray[10] = {2, 4, 8, 16, 32, 64, 128, 256, 512, 1}; // 0 to 9

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
    Serial.println("Failed to obtain time");
    blinkError();
    return;
  }
  Serial.println(&timeInfo, "%A, %B %d %Y %H:%M:%S zone %Z %z");
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
  Serial.print("Displaying digits: ");
  Serial.println(digits);
  bool isDate = false;
  if (digits < 0)
  { // we have a date, don't display the seconds positions
    isDate = true;
    digits = -digits;
  }

  digitalWrite(PIN_OE, LOW); // allow data input (Transparent mode)
  unsigned long var32 = 0;

  //-------- REG 1 -----------------------------------------------
  var32 = 0; // 32 bits all init to 0

  // 00 0000000000 0000000000 0000000000 0000000000 0000000000 0000000000
  //        s2         s1         m2         m1         h2         h1
  // -- 0987654321 0987654321 0987654321 0987654321 0987654321 0987654321

  if (!isDate)
  {
    var32 |= (unsigned long)(symbolArray[digits % 10]) << 20; // s2
  }
  digits /= 10;

  if (!isDate)
  {
    var32 |= (unsigned long)(symbolArray[digits % 10]) << 10; // s1
  }
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

void displayTime()
{
  // 123456
  digits = 0;
  digits += timeInfo.tm_hour * 10000;
  digits += timeInfo.tm_min * 100;
  digits += timeInfo.tm_sec;
  displayDigits();
}

void displayDate()
{
  // 123456
  digits = 0;
  digits += timeInfo.tm_mday * 10000;
  digits += timeInfo.tm_mon * 100;
  // year is not displayed so we mark it as negative
  digits *= -1;
  displayDigits();
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_OE, OUTPUT);

  Serial.begin(9600);
  Serial.setDebugOutput(true);

  SPI.begin(PIN_CLK, -1, PIN_DIN, -1);
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
