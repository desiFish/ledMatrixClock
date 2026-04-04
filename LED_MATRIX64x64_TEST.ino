/*
  LED_MATRIX64x64_TEST.ino

  Copyright (C) 2025-2026 desiFish

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// This is how many color levels the display shows - the more the slower the update
// #define PxMATRIX_COLOR_DEPTH 4

// Defines the speed of the SPI bus (reducing this may help if you experience noisy images)
// #define PxMATRIX_SPI_FREQUENCY 20000000

// Creates a second buffer for backround drawing (doubles the required RAM)
// #define PxMATRIX_double_buffer true

#include <PxMatrix.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include "RTClib.h"

// Pins for LED MATRIX
#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 16
hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

#define matrix_width 64
#define matrix_height 64

// This defines the 'on' time of the display is us. The larger this number,
// the brighter the display. If too large the ESP will crash
uint8_t display_draw_time = 30; // 30-70 is usually fine

PxMATRIX display(64, 64, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

// Some standard colors
uint16_t myRED = display.color565(255, 0, 0);
uint16_t myGREEN = display.color565(0, 255, 0);
uint16_t myBLUE = display.color565(0, 0, 255);
uint16_t myWHITE = display.color565(255, 255, 255);
uint16_t myYELLOW = display.color565(255, 255, 0);
uint16_t myCYAN = display.color565(0, 255, 255);
uint16_t myMAGENTA = display.color565(255, 0, 255);
uint16_t myBLACK = display.color565(0, 0, 0);

uint16_t myCOLORS[8] = {myRED, myGREEN, myBLUE, myWHITE, myYELLOW, myCYAN, myMAGENTA, myBLACK};

void IRAM_ATTR display_updater()
{
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(display_draw_time);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable)
{
  if (is_enable)
  {
    timer = timerBegin(1000000);
    timerAttachInterrupt(timer, &display_updater);
    timerAlarm(timer, 4000, true, 0);
    timerStart(timer);
  }
  else
  {
    timerDetachInterrupt(timer);
    timerStop(timer);
  }
}

RTC_DS1307 rtc;
char daysOfTheWeek[7][12] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// Variables to save date and time
String formattedDate;
String dayStamp;
String timeStamp;
// for creating task attached to CORE 0 of CPU
TaskHandle_t loop1Task;

String ntpServer = "";

String padNum(int num)
{
  return (num < 10 ? "0" : "") + String(num);
}

void setup()
{
  Serial.begin(9600);
  Wire.begin(32, 27);
  // Connect to WiFi
  WiFi.begin();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
  }
  Serial.println("Connected to WiFi");
  // Define your display layout here, e.g. 1/8 step, and optional SPI pins begin(row_pattern, CLK, MOSI, MISO, SS)
  display.begin(32);

  // Rotate display
  display.setRotate(true);

  // Flip display
  // display.setFlip(true);

  // Set the brightness of the panels (default is 255)
  display.setBrightness(50);

  display.clearDisplay();
  display.setTextColor(myCYAN);
  display.setCursor(0, 0);
  display.print("DIGITAL");
  display.setTextColor(myWHITE);
  display.setCursor(0, 10);
  display.print("CLOCK");
  display_update_enable(true);

  delay(500);
  for (int i = 0; i < 3; i++)
  {
    display.fillRect(0, 0, 64, 64, myCOLORS[i]);
    delay(100);
  }
  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC");
  }
  else
  {
    // Get current RTC time
    DateTime now = rtc.now();
    bool timeNeedsUpdate = false;

    // Check if RTC is not initialized (year = 1970) or RTC is not running
    if (now.year() == 1970 || !rtc.isrunning())
    {
      Serial.println("[DEBUG] RTC not initialized or not running, marking for update");
      timeNeedsUpdate = true;
    }

    // Get current day
    // byte currentDay = now.day();

    // Calculate days passed since last check
    // byte daysPassed = (currentDay - lastCheckedDay + 31) % 31;

    // Check if update is needed: timeNeedsUpdate flag OR 15+ days passed
    if (timeNeedsUpdate) // || daysPassed >= 15)
    {
      Serial.println("[INFO] Time update needed - attempting NTP sync");
      if (rtcTimeUpdater())
      {
        Serial.println("[INFO] Time updated successfully from NTP");
        timeNeedsUpdate = false;
      }
      else
      {
        Serial.println("[ERROR] Time update failed");
      }
      // Update lastCheckedDay regardless of success (to avoid constant attempts)
      // lastCheckedDay = currentDay;
    }
    else
    {
      Serial.println("[INFO] Time already updated, no action needed");
    }
  }

  xTaskCreatePinnedToCore(
      loop1,       // Task function.
      "loop1Task", // name of task.
      10000,       // Stack size of task
      NULL,        // parameter of the task
      1,           // priority of the task
      &loop1Task,  // Task handle to keep track of created task
      0);          // pin task to core 0
}

void loop1(void *pvParameters)
{
  for (;;)
  {
    delay(50);
  }
}

bool x = true;
void loop()
{
  // Blinking effect every 500ms
  static unsigned long lastBlinkTime = 0;
  static boolean blinkState = false;
  unsigned long currentTime = millis();

  if (currentTime - lastBlinkTime >= 800)
  {
    blinkState = !blinkState;
    lastBlinkTime = currentTime;
  }

  if (blinkState)
  {

    display.fillRect(22, 26, 2, 2, myWHITE);
    display.fillRect(22, 35, 2, 2, myWHITE);
  }
  else
  {
    display.fillRect(22, 26, 2, 2, myBLACK);
    display.fillRect(22, 35, 2, 2, myBLACK);
  }

  // Show content
  DateTime now = rtc.now();
  byte tempHour = now.twelveHour(); // Get the hour in 12-hour format

  // byte temp = int(rtc.getTemperature()); // Get the temperature in Celsius, rounded to the nearest integer

  String dateString = padNum(now.day()) + "/" + padNum(now.month()) + "/" + String(now.year()); // Format date string

  if (x || now.second() == 0)
  {
    x = false;
    display.clearDisplay();

    display.setFont(NULL);
    display.setTextColor(myGREEN);
    display.setCursor(3, 7);
    display.print(dateString);

    display.setFont(&FreeSans9pt7b);
    display.setTextSize(1);
    display.setTextColor(myWHITE);
    display.setCursor(0, 37);
    display.print(padNum(tempHour));

    display.setCursor(26, 37);
    display.print(padNum(now.minute()));
    display.setFont(NULL);
    display.print(now.isPM() ? " PM" : " AM");

    display.setTextColor(myYELLOW);
    display.setCursor(3, 50);
    display.print(daysOfTheWeek[now.dayOfTheWeek()]);
    display.setCursor(30, 50);
    display.print("20.3C");
  }

  delay(100);
}

bool rtcTimeUpdater()
{
  String debugLog = "";

  if (!rtc.begin())
  {
    Serial.println("[ERROR] RTC not found - cannot update time");
    return false;
  }

  // Early WiFi validation - check connection status first (WL_CONNECTED = 3)
  int wifiStatus = WiFi.status();
  if (wifiStatus != WL_CONNECTED)
  {
    char buffer[100];
    sprintf(buffer, "[WIFI_ERROR] WiFi not connected. Status: %d (need 3). IP: %s", wifiStatus, WiFi.localIP().toString().c_str());

    Serial.println(buffer);
    return false;
  }

  // Set default NTP server if empty
  if (ntpServer.length() == 0)
  {
    ntpServer = "pool.ntp.org";
  }

  // Array of fallback NTP servers to try
  const char *ntpServers[] = {"pool.ntp.org", "time.nist.gov", "time.cloudflare.com"};
  int numServers = 3;
  bool updated = false;
  String lastError = "";

  for (int attempt = 0; attempt < numServers && !updated; attempt++)
  {
    String currentServer = (attempt == 0) ? ntpServer : String(ntpServers[attempt]);
    char attemptLog[150];
    sprintf(attemptLog, "[NTP_ATTEMPT_%d] Trying server: %s | TZ offset: %d sec", attempt + 1, currentServer.c_str(), 19800);
    debugLog += attemptLog;
    debugLog += " | ";
    Serial.println(attemptLog);

    // Recreate NTPClient with current server (CRITICAL for each attempt)
    timeClient = NTPClient(ntpUDP, currentServer.c_str(), 19800);
    timeClient.begin();
    delay(300); // Extended delay for UDP + DNS

    // Attempt NTP update with retries
    for (int retry = 0; retry < 3 && !updated; retry++)
    {
      if (retry > 0)
      {
        delay(800);
      }

      updated = timeClient.update();

      char retryLog[120];
      if (updated)
      {
        sprintf(retryLog, "[SUCCESS] Update returned true on %s (retry %d)", currentServer.c_str(), retry);
        Serial.println(retryLog);
        debugLog += retryLog;
        break;
      }
      else
      {
        sprintf(retryLog, "[RETRY_%d_FAIL] %s returned false", retry + 1, currentServer.c_str());
        Serial.println(retryLog);
        lastError = retryLog;
      }
    }
  }

  if (!updated)
  {
    char finalError[180];
    sprintf(finalError, "[NTP_FAILED] All %d servers failed. WiFi status: %d. Last attempt: %s",
            numServers, WiFi.status(), lastError.c_str());
    Serial.println(finalError);
    return false;
  }

  // Check if time is actually set
  if (!timeClient.isTimeSet())
  {
    Serial.println("[TIME_NOT_SET] NTP sync returned success but time not set in client.");
    return false;
  }

  // Validate epoch time
  time_t rawtime = timeClient.getEpochTime();
  if (rawtime < 1000000000UL)
  {
    char epochError[130];
    sprintf(epochError, "[EPOCH_ERROR] Invalid NTP epoch: %ld (min: 1000000000). Year check failed.", rawtime);
    Serial.println(epochError);
    return false;
  }

  // Update RTC with validated time
  DateTime dt(rawtime);
  rtc.adjust(dt);

  char successMsg[150];
  sprintf(successMsg, "[SUCCESS] RTC synced to: %04d-%02d-%02d %02d:%02d:%02d",
          dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  Serial.println(successMsg);
  return true;
}
