#include <WiFi.h>
#include "time.h"

#define PIN_LED 26
#define PIN_DIN 13
#define PIN_CLK 14
#define PIN_OE 27
#define SHIFT_LSB_FIRST false
#define BUFSIZE 12
#define PRESCALER 15

const char *ssid = "***REMOVED***";
const char *wifipw = "***REMOVED***";

int maxDigits = 6;
String driverSetupStr;
hw_timer_t *ESP32timer = NULL;
unsigned long intCounter = 0;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
bool displayON = true;

byte digitPins[6][10] = {
    {130, 121, 122, 123, 124, 125, 126, 127, 128, 129}, // sec  1
    {120, 111, 112, 113, 114, 115, 116, 117, 118, 119}, // sec  10
    {110, 101, 102, 103, 104, 105, 106, 107, 108, 109}, // min   1
    {30, 21, 22, 23, 24, 25, 26, 27, 28, 29},           // min  10
    {20, 11, 12, 13, 14, 15, 16, 17, 18, 19},           // hour  1
    {10, 1, 2, 3, 4, 5, 6, 7, 8, 9}                     // hour 10
};

uint32_t DRAM_ATTR bitBuffer0 = 0; // new digits
uint32_t DRAM_ATTR bitBuffer1 = 0;
uint32_t DRAM_ATTR oldBitBuffer0 = 0; // old digits
uint32_t DRAM_ATTR oldBitBuffer1 = 0;
int DRAM_ATTR animPtr = 0;

// Brightness PWM timing. Greater value => slower brightness PWM frequency
//  Suggested values: 100000 / 20000, for faster PWM: 50000 / 10000
uint32_t DRAM_ATTR PWMrefresh = 100000;
uint32_t DRAM_ATTR PWM_min = 6000;

uint32_t DRAM_ATTR time1 = 2000;
uint32_t DRAM_ATTR time2 = 2000;
uint32_t DRAM_ATTR offTime = 2000;
uint32_t DRAM_ATTR brightness = 0;
uint32_t DRAM_ATTR PWMtimeBrightness;

byte DRAM_ATTR digit[BUFSIZE];
byte DRAM_ATTR newDigit[BUFSIZE];
byte DRAM_ATTR oldDigit[BUFSIZE];
boolean DRAM_ATTR digitDP[BUFSIZE]; // actual value to put to display
boolean digitsOnly = true;          // only 0..9 numbers are possible to display?
byte DRAM_ATTR animMask[BUFSIZE];   // 0 = no animation mask is used

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
  struct tm timeinfo;

  Serial.println("Setting up time");
  configTime(0, 0, "pool.ntp.org"); // First connect to NTP server, with 0 TZ offset
  if (!getLocalTime(&timeinfo))
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
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time 1");
    blinkError();
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S zone %Z %z ");
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

void IRAM_ATTR shift(uint32_t Data)
{
  static boolean b;

  for (uint32_t i = 0; i < 32; i++)
  {
    digitalWrite(PIN_CLK, HIGH);
    for (int t = 0; t < 20; t++)
      asm volatile("nop");
    if (SHIFT_LSB_FIRST)
      b = ((Data & (uint32_t(1) << i))) > 0; // LSB first
    else
      b = (Data & (uint32_t(1) << (31 - i))) > 0; // MSB first
    digitalWrite(PIN_DIN, b);
    for (int t = 0; t < 20; t++)
      asm volatile("nop");
    digitalWrite(PIN_CLK, LOW); // falling CLK  to store DIN
    for (int t = 0; t < 20; t++)
      asm volatile("nop");
  }
  digitalWrite(PIN_CLK, HIGH);
  for (int t = 0; t < 20; t++)
    asm volatile("nop");
}

void clearTubes()
{
  shift(0);
  shift(0);
}

void startTimer()
{ // ESP_INTR_FLAG_IRAM
  //  https://techtutorialsx.com/2017/10/07/esp32-arduino-timer-interrupts/
  ESP32timer = timerBegin(0, PRESCALER, true); // set prescaler to 80 -> 1MHz signal, true = edge generated signal
  timerAttachInterrupt(ESP32timer, &writeDisplay, true);
  timerAlarmWrite(ESP32timer, 1000, true); // 100millisec, no repeat
  timerAlarmEnable(ESP32timer);
}

void setup_pins()
{
  Serial.println("Setup pins -  HV5122 Nixie driver...");
  Serial.print("- CLK   : GPIO");
  Serial.println(PIN_CLK);
  Serial.print("- DATAIN: GPIO");
  Serial.println(PIN_DIN);
  Serial.print("- OUTPUT_ENABLE: GPIO");
  Serial.println(PIN_OE);
  pinMode(PIN_CLK, OUTPUT);
  pinMode(PIN_DIN, OUTPUT);
  pinMode(PIN_OE, OUTPUT);
  digitalWrite(PIN_CLK, HIGH);
  digitalWrite(PIN_OE, LOW);
  clearTubes();
  startTimer();
}

void IRAM_ATTR writeDisplay()
{
  static DRAM_ATTR byte state = 0;
  int timer = PWMrefresh;

  intCounter++;

  portENTER_CRITICAL_ISR(&timerMux);
  noInterrupts();
  digitalWrite(PIN_OE, LOW); // OFF
  switch (state)
  {       // state machine...
  case 0: // show old character
    if (time1 >= 1000)
    {
      timer = time1;
      shift(oldBitBuffer1);
      shift(oldBitBuffer0);
      state = 1;
      break;
    }
  case 1: // show new character
    if (time2 >= 1000)
    {
      timer = time2;
      shift(bitBuffer1);
      shift(bitBuffer0);
      if (offTime < 500)
        state = 0;
      else
        state = 2;
      break;
    }
  case 2: // blank display
    timer = offTime;
    state = 3;
    break;
  } // end switch

  if (timer < 500)
    timer = 500; // safety only...

  if ((state == 3) || (brightness == 0))
  {                            // OFF state, blank digit
    digitalWrite(PIN_OE, LOW); // OFF
    state = 0;
  }
  else
  {                             // ON state
    digitalWrite(PIN_OE, HIGH); // ON
  }

  portEXIT_CRITICAL_ISR(&timerMux);
  timerAlarmWrite(ESP32timer, timer, true);
  timerAlarmEnable(ESP32timer);
  interrupts();
}

void writeDisplaySingle()
{
  static byte lastAnimMask[BUFSIZE];
  static byte lastDigit[BUFSIZE];
  static unsigned long lastRun = 0;
  uint32_t b0, b1, ob0, ob1; // temp buffers
  byte num;

  if ((memcmp(digit, lastDigit, maxDigits) == 0) && (memcmp(lastDigit, newDigit, maxDigits) == 0) && (memcmp(animMask, lastAnimMask, maxDigits) == 0))
    return;
  // if ((millis()-lastRun)<5) return;
  lastRun = millis();

  memcpy(lastAnimMask, animMask, maxDigits);
  memcpy(lastDigit, digit, maxDigits);

  animPtr = 0;
  b0 = 0;
  b1 = 0;
  for (int i = 0; i < maxDigits; i++)
  { // Set the clock digits new values
    num = digit[i];
    if (animMask[i] > 0)
    {
      animPtr = animMask[i];
      num = newDigit[i];
    }
    if (num < 10)
    {
      if (digitPins[i][num] < 100)
      {
        b0 |= (uint32_t)(1 << (digitPins[i][num] - 1));
        // DPRINT(digitPins[i][num]); DPRINT(":");
      } // chip0
      else
      {
        b1 |= (uint32_t)(1 << (digitPins[i][num] - 101));
        // DPRINT(digitPins[i][num]);DPRINT(":");} //chip1
      }
    }
  } // end for i

  if (animPtr > 0)
  {
    ob0 = 0;
    ob1 = 0;
    for (int i = 0; i < maxDigits; i++)
    { // Set the clock digits old values
      num = oldDigit[i];
      if (num < 10)
      {
        if (digitPins[i][num] < 100)
        {
          ob0 |= (uint32_t)(1 << (digitPins[i][num] - 1));
          // DPRINT(digitPins[i][num]); DPRINT(":");
        } // chip0
        else
        {
          ob1 |= (uint32_t)(1 << (digitPins[i][num] - 101));
          // DPRINT(digitPins[i][num]);DPRINT(":");} //chip1
        }
      }
    } // end for i
  }

  for (int i = 0; i < maxDigits - 1; i++)
  { // Set the extra decimal point dots
    if (digitDP[i] && digitPins[maxDigits][i] > 0)
    {
      if (digitPins[maxDigits][i] < 100)
      {
        b0 |= (uint32_t)(1 << (digitPins[maxDigits][i] - 1));  // chip0
        ob0 |= (uint32_t)(1 << (digitPins[maxDigits][i] - 1)); // chip0
      }
      else
      {
        b1 |= (uint32_t)(1 << (digitPins[maxDigits][i] - 101));  // chip1
        ob1 |= (uint32_t)(1 << (digitPins[maxDigits][i] - 101)); // chip1
      }
    }
  } // end for i

  brightness = displayON ? dayBright : nightBright;
  if (brightness > MAXBRIGHTNESS)
    brightness = MAXBRIGHTNESS; // only for safety

  if (autoBrightness && displayON)
    PWMtimeBrightness = max(PWM_min, (PWMrefresh * lx) / MAXIMUM_LUX);
  else
    PWMtimeBrightness = max(PWM_min, (PWMrefresh * brightness) / MAXBRIGHTNESS);

  if ((animPtr == 0) || (PWMtimeBrightness < 15000))
  { // no animation #5
    ob0 = b0;
    ob1 = b1;
    animPtr = 10; // to make equal timing
  }

  time1 = PWMtimeBrightness * (20 - animPtr) / 20;
  if (time1 < 1000)
    time1 = 0;
  if (time1 > (PWMtimeBrightness - 1000))
    time1 = PWMtimeBrightness;
  time2 = PWMtimeBrightness - time1;
  offTime = PWMrefresh - time1 - time2;

  bitBuffer0 = b0;
  bitBuffer1 = b1;
  oldBitBuffer0 = ob0;
  oldBitBuffer1 = ob1;
}

void setup()
{
  pinMode(PIN_LED, OUTPUT);
  setup_pins();

  Serial.begin(115200);
  Serial.setDebugOutput(true);
  startWifi();

  initTime("CET-1CEST,M3.5.0,M10.5.0/3");
  printLocalTime();
}

void loop()
{
  while (1)
  {
    delay(1000);
    printLocalTime();
  }
}
