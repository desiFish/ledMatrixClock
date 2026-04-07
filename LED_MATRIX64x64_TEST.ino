/*
  LED_MATRIX64x64_TEST.ino - Digital Clock for 64x64 LED Matrix

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

  OPTIONAL CONFIGURATION:
  Uncomment these lines to customize display behavior
*/

// #define PxMATRIX_COLOR_DEPTH 4                   // Color levels (more = slower update)
// #define PxMATRIX_SPI_FREQUENCY 20000000          // SPI speed (reduce if noisy)
// #define PxMATRIX_double_buffer true              // Extra buffer for smooth drawing

// ============================================================================
// SOFTWARE VERSION
// ============================================================================

#define SW_VERSION "1.0.0-beta"

// ============================================================================
// INCLUDE CONFIGURATION AND HELPER FUNCTIONS
// ============================================================================

#include "config.h"

// ============================================================================
// SETUP - Initialize hardware, display, and connectivity
// ============================================================================
bool timeNeedsUpdate = false;
String errorFlag = "";
void setup()
{
  Serial.begin(9600);
  Wire.begin(32, 27);

  pinMode(buzzerPin, OUTPUT);
  pinMode(switch1Pin, INPUT);
  pinMode(switch2Pin, INPUT);

  digitalWrite(buzzerPin, HIGH);
  delay(200);
  digitalWrite(buzzerPin, LOW);

  // ---- Initialize WiFi ----
  WiFi.begin();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
  }
  Serial.println("Connected to WiFi");

  // ---- Initialize Display ----
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

  // ---- Initialize RTC and Synchronize Time ----
  if (!rtc.begin())
  {
    errorFlag = "RTC ERROR";
    Serial.println("Couldn't find RTC");
  }
  else
  {
    // Get current RTC time
    DateTime now = rtc.now();

    // Check if RTC is not initialized (year = 1970) or RTC is not running
    if (now.year() == 1970 || !rtc.isrunning())
    {
      Serial.println("RTC not initialized or not running, marking for update");
      timeNeedsUpdate = true;
    }
  }

  if (!temp117.begin()) // Initialize TMP117 and verify the device is present
  {
    errorFlag = "TMP ERROR";
    Serial.println("TMP117 init failed.");
  }
  else
  {
    temp117.setShutdownMode();
    Serial.println("TMP117 initialized successfully.");

    // Start the first one-shot conversion immediately
    lastRequestTime = millis() - readInterval;
  }

  // Initialize INA219 and verify the device is present
  if (!ina.begin(&Wire))
  {
    errorFlag = "INA ERROR";
    Serial.println(F("ERROR: INA219 @0x40 not found"));
  }
  else
  {
    // Config: 32V, ±320mV, 12-bit x16 avg, continuous shunt+bus
    bool range16V = true; // true: 16V, false: 32V
    uint8_t pga = 3;
    uint8_t badc = 0x0B;
    uint8_t sadc = 0x0B;
    uint8_t mode = 0x07;
    ina.configure(range16V, pga, badc, sadc, mode);

    // Auto calibration: set per your shunt & range
    float maxExpected_A = 1.0f;
    float shunt_Ohms = 0.1f;
    ina.calibrateAuto(maxExpected_A, shunt_Ohms);

    Serial.println(F("Single INA219 ready.\n"));
  }
  // LIGHT SENSOR
  if (lightMeter.begin(BH1750::ONE_TIME_HIGH_RES_MODE_2))
  {
    Serial.println(F("BH1750 Advanced begin"));
  }
  else
  {
    errorFlag = "BHT ERROR";
    Serial.println(F("Error initialising BH1750"));
  }

  // ---- Create Core 0 Task ----
  xTaskCreatePinnedToCore(
      loop1,       // Task function.
      "loop1Task", // name of task.
      10000,       // Stack size of task
      NULL,        // parameter of the task
      1,           // priority of the task
      &loop1Task,  // Task handle to keep track of created task
      0);          // pin task to core 0
}

float tempC = 0.0f;
byte currentDay = 0;
byte lastCheckedDay = 0;

// ============================================================================
// CORE 0 TASK - Secondary loop for dual-core operation
// ============================================================================

void loop1(void *pvParameters)
{
  for (;;)
  {
    if (!conversionRequested && (millis() - lastRequestTime >= readInterval))
    {
      temp117.setOneShotMode();
      conversionRequested = true;
    }

    if (conversionRequested && temp117.dataReady())
    {
      tempC = temp117.readTempC();
      Serial.print("Temperature: ");
      Serial.print(tempC);
      Serial.println(" C");

      conversionRequested = false;
      lastRequestTime = millis();
    }

    static byte targetBrightness = 0;
    static float lux = 0;
    static byte currentBrightness = 50;
    static bool isDark = true;
    if (millis() - lastLightRead > lightInterval)
    {
      lastLightRead = millis();

      lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE_2);

      // Block until measurement ready (with timeout for safety)
      unsigned long start = millis();
      while (!lightMeter.measurementReady(true))
      {
        if (millis() - start > 3000)
        { // 3s timeout
          Serial.println("[ERROR] Light sensor timeout!");
          break;
        }
        yield();
      }

      // Read lux (may be stale if timeout triggered)
      lux = lightMeter.readLightLevel();

      // Map lux to target brightness
      byte val1 = constrain(lux, 1, 120);            // Limit lux to 1-120 for mapping
      targetBrightness = map(val1, 1, 120, 10, 100); // Map to 40-50 range (max brightness = 50)

      Serial.print("[DEBUG] Lux = ");
      Serial.print(lux);
      Serial.print(", targetBrightness = ");
      Serial.print(targetBrightness);
      Serial.print(", isDark = ");
      Serial.println(isDark);
    }

    // --- Brightness animation every 200ms ---
    if (millis() - lastBrightnessUpdate > brightnessInterval)
    {
      lastBrightnessUpdate = millis();

      byte previousBrightness = currentBrightness; // store old value
      // Update darkness flag
      isDark = lux <= 1;

      if (isDark)
      {
        currentBrightness = 4; // force off
      }
      else
      {
        // exponential moving average (EMA)
        float alpha = 0.7; // smoother 0.7-0.9
        float tempBrightness = currentBrightness;
        tempBrightness = tempBrightness * alpha + targetBrightness * (1.0 - alpha);
        currentBrightness = (byte)tempBrightness;
      }

      // Only write PWM if it changed
      if (currentBrightness != previousBrightness)
      {
        display.setBrightness(currentBrightness);
        Serial.print("[DEBUG] Brightness = ");
        Serial.println(currentBrightness);
      }
    }

    if (millis() - lastCurrentTime >= currentInterval)
    {
      if (ina.conversionReady())
      {
        float vBus_V = ina.readBusVoltage();
        float vShunt_mV = ina.readShuntVoltage();
        float current_mA = ina.readCurrent();
        float power_mW = ina.readPower();
        bool ovf = ina.overflow();

        // Check for voltage errors
        if (vBus_V < 4.7 || vBus_V > 5.5 || ovf)
        {
          errorFlag = "VOLT " + String(vBus_V, 2);
        }
        else if (errorFlag.startsWith("VOLT"))
        {
          errorFlag = ""; // Clear voltage error if voltage is now normal
        }

        Serial.print(F("Bus: "));
        Serial.print(vBus_V, 3);
        Serial.print(F(" V"));
        // Serial.print(vShunt_mV, 3);
        Serial.print(F(" mV  Curr: "));
        Serial.print(current_mA, 2);
        Serial.print(F(" mA  Power: "));
        Serial.print(power_mW, 1);
        Serial.print(F(" mW"));

        if (ovf)
          Serial.print(F("  [OVF]"));
        Serial.println();
      }
      lastCurrentTime = millis();
    }

    if (digitalRead(switch1Pin) == HIGH)
    {
      Serial.println("Switch1 pressed");
    }

    if (digitalRead(switch2Pin) == HIGH)
    {
      Serial.println("Switch2 pressed");
    }

    // Calculate days passed since last check
    byte daysPassed = (currentDay - lastCheckedDay + 31) % 31;

    // Check if update is needed: timeNeedsUpdate flag OR 15+ days passed
    if (timeNeedsUpdate || daysPassed >= 5)
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
      lastCheckedDay = currentDay;
    }

    delay(50);
  }
}

// ============================================================================
// MAIN LOOP - Display update and time rendering
// ============================================================================

bool x = true;
void loop()
{
  if (!timeNeedsUpdate)
  {
    // ---- Display Blinking Colon Effect ----
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

    // ---- Read and Format Time Data ----
    DateTime now = rtc.now();
    byte tempHour = now.twelveHour(); // Get the hour in 12-hour format

    // byte temp = int(rtc.getTemperature()); // Get the temperature in Celsius, rounded to the nearest integer
    currentDay = now.day();

    String dateString = padNum(currentDay) + "/" + padNum(now.month()) + "/" + String(now.year()); // Format date string

    // ---- Update Display Every Second ----
    if (x || now.second() == 0)
    {
      x = false;
      display.clearDisplay();

      // Display date
      display.setFont(NULL);
      display.setTextColor(myGREEN);
      display.setCursor(3, 7);
      display.print(dateString);

      // Display time
      display.setFont(&FreeSans9pt7b);
      display.setTextSize(1);
      display.setTextColor(myWHITE);
      display.setCursor(0, 37);
      display.print(padNum(tempHour));

      display.setCursor(26, 37);
      display.print(padNum(now.minute()));
      display.setFont(NULL);
      display.print(now.isPM() ? " PM" : " AM");

      // Display error if present, else day and temperature
      if (errorFlag.length() > 0)
      {
        display.setTextColor(myRED);
        display.setCursor(3, 50);
        display.print(errorFlag);
      }
      else
      {
        display.setTextColor(myYELLOW);
        display.setCursor(3, 50);
        display.print(daysOfTheWeek[now.dayOfTheWeek()]);
        display.setCursor(30, 50);
        display.print(tempC, 1); // Display temperature with 1 decimal place
        display.print("C");
      }
    }
  }
  else
  {
    display.clearDisplay();
    display.setFont(NULL);
    display.setTextColor(myRED);
    display.setCursor(0, 0);
    display.print("RTC UPDATE");
    display.setCursor(0, 10);
    display.print("IN");
    display.setCursor(0, 20);
    display.print("PROGRESS...");
    delay(1000);
  }

  delay(100);
}

// ============================================================================
// TIME SYNCHRONIZATION - NTP and RTC management
// ============================================================================

bool rtcTimeUpdater()
{
  String debugLog = "";

  // ---- Validate RTC Availability ----
  if (!rtc.begin())
  {
    Serial.println("[ERROR] RTC not found - cannot update time");
    return false;
  }

  // ---- Validate WiFi Connection ----
  // Early WiFi validation - check connection status first (WL_CONNECTED = 3)
  int wifiStatus = WiFi.status();
  if (wifiStatus != WL_CONNECTED)
  {
    char buffer[100];
    sprintf(buffer, "[WIFI_ERROR] WiFi not connected. Status: %d (need 3). IP: %s", wifiStatus, WiFi.localIP().toString().c_str());

    Serial.println(buffer);
    return false;
  }

  // ---- Configure NTP Server ----
  // Set default NTP server if empty
  if (ntpServer.length() == 0)
  {
    ntpServer = "pool.ntp.org";
  }

  // ---- Attempt NTP Synchronization with Fallback Servers ----
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

  // ---- Handle Synchronization Failure ----
  if (!updated)
  {
    char finalError[180];
    sprintf(finalError, "[NTP_FAILED] All %d servers failed. WiFi status: %d. Last attempt: %s",
            numServers, WiFi.status(), lastError.c_str());
    Serial.println(finalError);
    return false;
  }

  // ---- Validate Time From NTP ----
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

  // ---- Update RTC with Synchronized Time ----
  DateTime dt(rawtime);
  rtc.adjust(dt);

  char successMsg[150];
  sprintf(successMsg, "[SUCCESS] RTC synced to: %04d-%02d-%02d %02d:%02d:%02d",
          dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  Serial.println(successMsg);
  return true;
}
