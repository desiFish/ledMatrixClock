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
// INCLUDE CONFIGURATION AND HELPER FUNCTIONS
// ============================================================================

#include "config.h"

// ============================================================================
// ABOUT THIS PROGRAM
// ============================================================================

// This sketch runs a 64x64 LED matrix clock with automatic time updates,
// temperature monitoring, power measurement, and adaptive brightness.
// The display shows the current date, time, day of the week, and temperature.
// If something goes wrong during startup or the power bus gets out of range,
// the panel displays a simple error message instead of normal data.

// ============================================================================
// SETUP - Initialize hardware, display, and connectivity
// ============================================================================
// The setup() function only runs once, and it brings the hardware online.
// This includes WiFi, the display panel, the clock chip, the temperature sensor,
// the power sensor, and the light sensor.
void setup()
{
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, RXPin, TXPin);
  Wire.begin(32, 27);

  pinMode(buzzerPin, OUTPUT);
  pinMode(switch1Pin, INPUT);
  pinMode(switch2Pin, INPUT);

  digitalWrite(buzzerPin, HIGH);
  delay(200);
  digitalWrite(buzzerPin, LOW);

  /*WiFi.mode(WIFI_STA);
  WiFi.begin("SonyBraviaX400", "66227617975PsA#");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
  {
    delay(200);
  }*/

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

  delay(1000);

  for (int i = 0; i < 3; i++)
  {
    display.fillRect(0, 0, 64, 64, myCOLORS[i]);
    delay(500);
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
bool hourlyAlarmTriggered = false;
byte lastAlarmHour = 255; // Track previous hour to detect change
bool hourlyRtcUpdateTriggered = false;
byte lastHour = 255; // Track previous hour to detect change
bool isDark = true;

// Animation for RTC update progress display
static unsigned long lastAnimationTime = 0;
static byte animationFrame = 0; // 0-3 for ".", "..", "...", ""

// ============================================================================
// ============================================================================
// CORE 0 TASK - Secondary loop for background sensor updates
// ============================================================================
// This task runs on the second core and handles the slower sensor loops.
// It reads temperature, adjusts display brightness, checks the power bus,
// and decides when the clock needs a time sync.
void loop1(void *pvParameters)
{
  for (;;)
  {
    // ---- Handle hourly alarm buzzer ----
    if (hourlyAlarmTriggered && !isDark) // Only buzz if it's not dark (to avoid disturbing sleep)
    {
      digitalWrite(buzzerPin, HIGH); // Buzzer on
      delay(1000);                   // 1 second buzz
      digitalWrite(buzzerPin, LOW);  // Buzzer off
      hourlyAlarmTriggered = false;  // Reset flag
    }

    // ---- Handle hourly RTC update ----
    if (hourlyRtcUpdateTriggered)
    {
      if (rtcTimeUpdater())
      {
        Serial.println("[INFO] Hourly RTC update successful");
      }
      else
      {
        Serial.println("[ERROR] Hourly RTC update failed");
      }
      hourlyRtcUpdateTriggered = false; // Reset flag
    }

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
    if (millis() - lastLightRead > lightInterval)
    {
      lastLightRead = millis();

      lightMeter.configure(BH1750::ONE_TIME_HIGH_RES_MODE_2);

      // Block until measurement ready (with timeout for safety)
      unsigned long start = millis();
      bool measurementSuccess = false;
      while (!lightMeter.measurementReady(true))
      {
        if (millis() - start > 3000)
        { // 3s timeout
          Serial.println("[ERROR] Light sensor timeout!");
          break;
        }
        yield();
      }

      // Only update lux if measurement succeeded
      if (millis() - start <= 3000)
      {
        lux = lightMeter.readLightLevel();
        measurementSuccess = true;
      }

      // Map lux to target brightness. 10 is dim, 50 is the brightest value we allow.
      if (measurementSuccess)
      {
        byte val1 = constrain(lux, 1, 120);           // Keep lux in the expected range
        targetBrightness = map(val1, 1, 120, 10, 50); // Map full light range to 10-50 brightness
      }

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
        if (vBus_V < 4.7 || vBus_V > 5.2 || ovf)
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
      delay(200);                      // simple debounce
      hourlyRtcUpdateTriggered = true; // Trigger an immediate RTC update on the next loop
    }

    if (digitalRead(switch2Pin) == HIGH)
    {
      Serial.println("Switch2 pressed");
      delay(200); // simple debounce
    }

    // Calculate how many days have passed since the last time sync.
    // Using the day-of-month value keeps this simple and avoids a full
    // date-time difference calculation.
    byte daysPassed = (currentDay - lastCheckedDay + 31) % 31;

    // If the clock was never synced, or five days have gone by, do an update.
    if (timeNeedsUpdate || daysPassed >= 5)
    {
      timeNeedsUpdate = true; // Set this early to prevent multiple attempts in the same loop
      Serial.println("[INFO] Time update needed - attempting GPS sync");
      if (rtcTimeUpdater())
      {
        Serial.println("[INFO] Time updated successfully from GPS");
        timeNeedsUpdate = false;
      }
      else
      {
        Serial.println("[ERROR] Time update failed");
      }
      // Update lastCheckedDay regardless of success (to avoid constant attempts)
      lastCheckedDay = currentDay;
      x = true; // Force display refresh on next loop to show updated time or error
    }
    delay(50);
  }
}

// ============================================================================
// MAIN LOOP - Display update and time rendering
// ============================================================================
// This is the frame loop for the LED matrix. It redraws the clock and
// updates the blinking colon, while the background task handles sensors and sync.
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

    currentDay = now.day();

    // ---- Trigger hourly alarm when hour changes ----
    if (now.hour() != lastAlarmHour && now.second() == 0)
    {
      hourlyAlarmTriggered = true;
      lastAlarmHour = now.hour();
    }

    // ---- Trigger hourly RTC update when hour changes ----
    if (now.hour() != lastHour)
    {
      hourlyRtcUpdateTriggered = true;
      lastHour = now.hour();
      Serial.println("[INFO] Hourly RTC update triggered");
    }

    String dateString = padNum(currentDay) + "/" + padNum(now.month()) + "/" + String(now.year()); // Format date string

    // ---- Update Display Every Second ----
    if (x || now.second() == 0)
    {
      x = false;
      display.clearDisplay();

      // Display date
      display.setFont(NULL);
      display.setTextColor(myCYAN);
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

      // If something went wrong while initializing hardware or during
      // voltage monitoring, show that error instead of the normal footer.
      if (errorFlag.length() > 0)
      {
        display.setTextColor(myRED);
        display.setCursor(3, 50);
        display.print(errorFlag);
      }
      else
      {
        display.setTextColor(myMAGENTA);
        display.setCursor(3, 50);
        display.print(daysOfTheWeek[now.dayOfTheWeek()]);

        // Select temperature color based on value
        uint16_t tempColor = myYELLOW; // default 29-31°C
        if (tempC >= 32)
        {
          tempColor = myRED; // Red for 32°C and above
        }
        else if (tempC <= 29)
        {
          tempColor = myBLUE; // Blue for 29°C and below
        }

        display.setTextColor(tempColor);
        display.setCursor(30, 50);
        display.print(tempC, 1); // Display temperature with 1 decimal place
        display.setCursor(52, 45);
        display.print(".");
        display.setCursor(57, 50);
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
    display.print("PROGRESS");

    // Animated ellipsis
    if (millis() - lastAnimationTime > 300)
    {
      lastAnimationTime = millis();
      animationFrame = (animationFrame + 1) % 4;
    }

    display.setCursor(0, 30);
    switch (animationFrame)
    {
    case 0:
      display.print(".");
      break;
    case 1:
      display.print("..");
      break;
    case 2:
      display.print("...");
      break;
    case 3:
      display.print("");
      break; // blank for cycling effect
    }
  }
  delay(100);
}

// ============================================================================
// TIME SYNCHRONIZATION - GPS and RTC management
// ============================================================================

bool rtcTimeUpdater()
{
  bool gotFreshGpsData = false;
  while (Serial1.available())
  {
    if (gps.encode(Serial1.read()))
    { // process gps messages
      // when TinyGPSPlus reports new data...
      unsigned long age = gps.time.age();

      if (age < 300)
      {
        gotFreshGpsData = true;
        // Build tm struct with GPS UTC
        struct tm tmUTC = {};
        int gpsYear = gps.date.year();
        tmUTC.tm_year = gpsYear - 1900;      // struct tm wants "years since 1900"
        tmUTC.tm_mon = gps.date.month() - 1; // months 0-11
        tmUTC.tm_mday = gps.date.day();
        tmUTC.tm_hour = gps.time.hour();
        tmUTC.tm_min = gps.time.minute();
        tmUTC.tm_sec = gps.time.second();

        if (gpsYear < 2026)
          return false; // Reject GPS data if the year is before 2026, likely indicating a GPS read error

        // Convert to epoch (UTC)
        time_t t = mktime(&tmUTC);

        // Apply IST offset (+5:30 → 19800 seconds)
        t += 19800;

        // Save epoch and also convert to broken-down local time
        currentEpoch = t;
      }
    }
  }

  // If no fresh GPS data was received, skip the update
  if (!gotFreshGpsData)
  {
    Serial.println("[ERROR] No fresh GPS data (age too old). Skipping RTC update.");
    return false;
  }

  time_t rawtime = currentEpoch;
  if (rawtime < 1000000000UL)
  {
    char epochError[130];
    sprintf(epochError, "[EPOCH_ERROR] Invalid GPS epoch: %ld (min: 1000000000). Year check failed.", rawtime);
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

static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do
  {
    while (Serial1.available())
      gps.encode(Serial1.read());
  } while (millis() - start < ms);
}
