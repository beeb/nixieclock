#include <SPI.h>
#include <WiFi.h>
#include "time.h"

#define PIN_LED 26
#define PIN_DIN 13
#define PIN_CLK 14
#define PIN_OE 27
#define ON_TIME_US 1000

const char *ssid = "***REMOVED***";
const char *wifipw = "***REMOVED***";

struct tm timeInfo;
time_t prevTime = 0;
unsigned long lastRefresh = 0;

bool displayEnabled = false;
bool runningACP = false;
TaskHandle_t mainTask;

long digits = 0;               // number to display, negative means only 4 digits are shown (second positions are blank)
unsigned int brightness = 100; // percentage

unsigned int symbolArray[10] = {512, 1, 2, 4, 8, 16, 32, 64, 128, 256}; // 0 to 9

void blinkError()
{
  for (int i = 0; i < 10; i++)
  {
    digitalWrite(PIN_LED, HIGH);
    delay(100);
    digitalWrite(PIN_LED, LOW);
    delay(100);
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

// idea for brightness: add a delay parameter to the function and pause after the PIN_OE goes low for some time
// also call displayDigits permamently in the loop
// maybe actually call displayDigits from second core?
// https://randomnerdtutorials.com/esp32-dual-core-arduino-ide/
void IRAM_ATTR displayDigits(void *pvParameters)
{
  Serial.print("Task1 running on core ");
  Serial.println(xPortGetCoreID());
  for (;;)
  {
    delayMicroseconds(ON_TIME_US);
    if (!displayEnabled)
    {
      continue;
    }
    long digitsCopy;
    memcpy(&digitsCopy, &digits, sizeof(long));
    bool isDate = false;
    if (digitsCopy < 0)
    { // we have a date, don't display the seconds positions
      isDate = true;
      digitsCopy = -digitsCopy;
    }

    digitalWrite(PIN_OE, LOW); // allow data input (transparent mode, all outputs are LOW)
    unsigned long var32 = 0;

    //-------- REG 1 -----------------------------------------------
    var32 = 0; // 32 bits all init to 0

    // 00 0000000000 0000000000 0000000000 0000000000 0000000000 0000000000
    //        s2         s1         m2         m1         h2         h1
    // -- 0987654321 0987654321 0987654321 0987654321 0987654321 0987654321

    if (!isDate)
    {
      var32 |= (unsigned long)(symbolArray[digitsCopy % 10]) << 20; // s2
    }
    digitsCopy /= 10;

    if (!isDate)
    {
      var32 |= (unsigned long)(symbolArray[digitsCopy % 10]) << 10; // s1
    }
    digitsCopy /= 10;

    var32 |= (unsigned long)(symbolArray[digitsCopy % 10]); // m2
    digitsCopy /= 10;

    SPI.transfer(var32 >> 24);
    SPI.transfer(var32 >> 16);
    SPI.transfer(var32 >> 8);
    SPI.transfer(var32);

    //-------- REG 0 -----------------------------------------------
    var32 = 0;

    var32 |= (unsigned long)(symbolArray[digitsCopy % 10]) << 20; // m1
    digitsCopy /= 10;

    var32 |= (unsigned long)(symbolArray[digitsCopy % 10]) << 10; // h2
    digitsCopy /= 10;

    var32 |= (unsigned long)(symbolArray[digitsCopy % 10]); // h1
    digitsCopy /= 10;

    SPI.transfer(var32 >> 24);
    SPI.transfer(var32 >> 16);
    SPI.transfer(var32 >> 8);
    SPI.transfer(var32);

    if (!runningACP)
    {
      // dim brightness by forcing longer off time
      delayMicroseconds((ON_TIME_US * 100 - ON_TIME_US * min(brightness, (uint)100)) / min(brightness, (uint)100));
    }
    digitalWrite(PIN_OE, HIGH); // latching data (enables HV outputs according to registers)
    if (runningACP)
    {
      delay(100); // force longer on-time when performing ACP routine
    }
  }
}

void displayTime()
{
  digits = 0;
  digits += timeInfo.tm_hour * 10000;
  digits += timeInfo.tm_min * 100;
  digits += timeInfo.tm_sec;
}

void displayDate()
{
  digits = 0;
  digits += timeInfo.tm_mday * 10000;
  digits += (timeInfo.tm_mon + 1) * 100; // month is 0-11, we need to add 1
  // year is not displayed so we mark it as negative
  digits *= -1;
}

void ACP()
{
  // cycle through digits to avoid cathode poisoning
  runningACP = true;
  digits = 0;
  for (int i = 0; i < 10; i++)
  {
    delay(2000);
    digits += 111111;
  }
  runningACP = false;
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_OE, OUTPUT);
  digitalWrite(PIN_OE, LOW); // force all off

  Serial.begin(9600);

  SPI.begin(PIN_CLK, -1, PIN_DIN, -1); // we only use clock and MOSI
  SPI.setDataMode(SPI_MODE2);
  SPI.setClockDivider(SPI_CLOCK_DIV8); // SCK = 16MHz/8 = 2MHz

  startWifi();

  initTime("CET-1CEST,M3.5.0,M10.5.0/3");

  xTaskCreatePinnedToCore(displayDigits, "DisplayLoop", 10000, NULL, 1, &mainTask, 1);
}

void loop()
{
  if (millis() - lastRefresh < 200)
  {
    return;
  }
  lastRefresh = millis();
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

    // brightness control
    if (timeInfo.tm_hour < 6 || timeInfo.tm_hour >= 22)
    {
      brightness = 10;
    }
    else if (timeInfo.tm_hour < 8 || timeInfo.tm_hour >= 20)
    {
      brightness = 75;
    }
    else
    {
      brightness = 100;
    }

    if ((timeInfo.tm_sec >= 50) && (timeInfo.tm_sec < 55))
    {
      displayDate(); // sets digits to display
    }
    else if (timeInfo.tm_min % 10 == 7 && timeInfo.tm_sec == 15) // at xx:x7:15
    {
      // during day time we do anti-cathode poisoning only every 20 minutes
      if ((timeInfo.tm_hour >= 5 || timeInfo.tm_hour < 23) && (timeInfo.tm_min / 10) % 2 == 0)
      {
        displayTime(); // sets digits to display
      }
      else
      {
        ACP(); // blocking
      }
    }
    else
    {
      displayTime(); // sets digits to display
    }
  }
  if (!displayEnabled)
  {
    displayEnabled = true;
  }
}
