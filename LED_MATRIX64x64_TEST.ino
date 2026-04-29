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
// GLOBAL FLAGS
// ============================================================================
// Flag to skip jump detection after successful RTC sync to prevent false anomalies
bool skipNextJumpCheck = false;

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
  Wire.begin(32, 27);

  pinMode(buzzerPin, OUTPUT);
  pinMode(switch1Pin, INPUT);
  pinMode(switch2Pin, INPUT);

  digitalWrite(buzzerPin, HIGH);
  delay(200);
  digitalWrite(buzzerPin, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin("SonyBraviaX400", "66227617975PsA#");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000)
  {
    Serial.print(".");
    delay(200);
  }

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
    delay(1000);
  }

  // ---- Initialize System Time ----
  // Check if system time is valid (year should not be 1970)
  uint16_t currentYear = getYear();
  if (currentYear <= 1970)
  {
    Serial.println("[INFO] System time not initialized, marking for NTP update");
    timeNeedsUpdate = true;
  }
  else
  {
    Serial.print("[INFO] System time initialized to: ");
    Serial.print(currentYear);
    Serial.print("-");
    Serial.print(padNum(getMonth()));
    Serial.print("-");
    Serial.println(padNum(getDay()));
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

  x = true; // Force initial display update on first loop
}

float tempC = -99.0f;
bool hourlyAlarmTriggered = false;
byte lastAlarmHour = 255; // Track previous hour to detect change
bool isDark = true;
int wifiRssi = -127; // Stored WiFi RSSI for signal meter display

// WiFi signal display tracking
byte lastSignalDots = 255;                // Track previous signal strength to detect changes
uint16_t lastWifiColor = myBLACK;         // Track previous signal color
unsigned long lastWiFiUpdateTime = 0;     // Track last WiFi signal update time
unsigned long lastWiFiRssiUpdateTime = 0; // Track last WiFi RSSI reading time

// Display section tracking for localized updates
byte lastDisplayedDay = 255;
byte lastDisplayedMonth = 255;
int lastDisplayedYear = -1;
byte lastDisplayedHour = 255;
byte lastDisplayedMinute = 255;
bool lastDisplayedIsPM = false;
byte lastDisplayedDayOfWeek = 255;
float lastDisplayedTempC = -99.0f;
bool lastDisplayedErrorFlag = false;

// Animation for RTC update progress display
unsigned long lastAnimationTime = 0;
byte animationFrame = 0; // 0-3 for ".", "..", "...", ""

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

    // ---- Update stored WiFi RSSI every 5 seconds ----
    if (millis() - lastWiFiRssiUpdateTime >= 5000)
    {
      lastWiFiRssiUpdateTime = millis();
      if (WiFi.status() == WL_CONNECTED)
      {
        wifiRssi = WiFi.RSSI();
        Serial.print("[DEBUG] WiFi RSSI: ");
        Serial.println(wifiRssi);
      }
      else
      {
        wifiRssi = -127;
      }
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
    static byte currentBrightness = 0;
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
        if (vBus_V < 4.7 || vBus_V > 5.3 || ovf)
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
      Serial.println("Switch1 pressed - Requesting time update...");
      delay(200);             // simple debounce
      timeNeedsUpdate = true; // Trigger an immediate RTC update on the next loop
    }

    if (digitalRead(switch2Pin) == HIGH)
    {
      Serial.println("Switch2 pressed - Resetting...");
      delay(200);    // simple debounce
      ESP.restart(); // Restart the microcontroller
    }

    // ---- Background NTP Sync (every minute, silent, no display update) ----
    if ((millis() - lastBackgroundNtpTime >= 60000) && WiFi.status() == WL_CONNECTED)
    {
      lastBackgroundNtpTime = millis();
      Serial.println("[BACKGROUND] Attempting silent NTP sync...");
      // Attempt sync without triggering display updates
      rtcTimeUpdater();
      skipNextJumpCheck = true; // Skip jump detection after background sync
    }

    // Calculate how many days have passed since the last time sync.
    // Using the day-of-month value keeps this simple and avoids a full
    // date-time difference calculation.
    byte daysPassed = (currentDay - lastCheckedDay + 31) % 31;

    // If the clock was never synced, or five days have gone by, do an update.
    if (timeNeedsUpdate || daysPassed >= 1)
    {
      timeNeedsUpdate = true; // Set this early to prevent multiple attempts in the same loop
      Serial.println("[INFO] Time update needed - attempting NTP sync");
      if (rtcTimeUpdater())
      {
        Serial.println("[INFO] Time updated successfully from NTP");
        timeNeedsUpdate = false;
        skipNextJumpCheck = true; // Skip jump detection after successful sync
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
// RTC HELPER FUNCTIONS - Clean modular architecture
// ============================================================================

// System time validation: check time consistency
time_t doubleReadRTC()
{
  time_t now1 = getCurrentTime();
  delay(5);
  time_t now2 = getCurrentTime();

  // Check if the two reads differ by more than 2 seconds
  if (abs((long)(now2 - now1)) > 2)
  {
    Serial.println("[TIME_ERROR] Inconsistent time read detected");
    Serial.print("[TIME_DEBUG] Read 1: ");
    Serial.print(now1);
    Serial.print(" | Read 2: ");
    Serial.println(now2);
    return 1; // Return invalid epoch (1970)
  }

  return now2; // Use second read
}

// Basic range validation for all time fields
bool basicRangeCheck(time_t now)
{
  struct tm *timeinfo = localtime(&now);

  // Check hour (0-23)
  if (timeinfo->tm_hour > 23)
  {
    Serial.print("[TIME_ERROR] Invalid hour: ");
    Serial.println(timeinfo->tm_hour);
    return false;
  }

  // Check minute (0-59)
  if (timeinfo->tm_min > 59)
  {
    Serial.print("[TIME_ERROR] Invalid minute: ");
    Serial.println(timeinfo->tm_min);
    return false;
  }

  // Check second (0-59)
  if (timeinfo->tm_sec > 59)
  {
    Serial.print("[TIME_ERROR] Invalid second: ");
    Serial.println(timeinfo->tm_sec);
    return false;
  }

  // Check day (1-31)
  int day = timeinfo->tm_mday;
  if (day < 1 || day > 31)
  {
    Serial.print("[TIME_ERROR] Invalid day: ");
    Serial.println(day);
    return false;
  }

  // Check month (1-12)
  int month = timeinfo->tm_mon + 1;
  if (month < 1 || month > 12)
  {
    Serial.print("[TIME_ERROR] Invalid month: ");
    Serial.println(month);
    return false;
  }

  // Check year (2026-2100)
  int year = timeinfo->tm_year + 1900;
  if (year < 2026 || year > 2100)
  {
    Serial.print("[TIME_ERROR] Invalid year: ");
    Serial.println(year);
    return false;
  }

  return true;
}

// ============================================================================
// RTC SANITY CHECK - Main validation function (clean architecture)
// ============================================================================
// Returns true if RTC values are valid, false if corrupted/abnormal
bool rtcSanityCheck()
{
  static long lastUnix = 0;
  static bool firstCheck = true;

  // Get system time with validation
  time_t now = doubleReadRTC();

  // Basic range check on all fields
  if (!basicRangeCheck(now))
  {
    return false;
  }

  // Jump detection (only after first check AND not skipping due to recent sync)
  if (!firstCheck && !skipNextJumpCheck)
  {
    long timeDiff = now - lastUnix;

    // Allow up to 2 minutes (120 seconds) between checks
    if (timeDiff < 0 || timeDiff > 120)
    {
      Serial.print("[TIME_ERROR] Abnormal time jump: ");
      Serial.print(timeDiff);
      Serial.println(" sec");
      return false;
    }
  }

  // Clear the skip flag after this check
  if (skipNextJumpCheck)
  {
    skipNextJumpCheck = false;
  }

  // Update baseline for next check
  lastUnix = now;
  firstCheck = false;

  return true;
}

// ============================================================================
// WiFi SIGNAL DISPLAY - Update WiFi signal indicator
// ============================================================================
// Always displays WiFi signal strength
bool updateWiFiSignalDisplay()
{
  // Calculate current signal strength
  uint16_t wifiColor = myRED;
  byte signalDots = 0;

  if (wifiRssi >= -60)
  {
    wifiColor = myGREEN;
    signalDots = 5;
  }
  else if (wifiRssi >= -70)
  {
    wifiColor = myGREEN;
    signalDots = 4;
  }
  else if (wifiRssi >= -80)
  {
    wifiColor = myORANGE;
    signalDots = 3;
  }
  else if (wifiRssi >= -90)
  {
    wifiColor = myORANGE;
    signalDots = 2;
  }
  else if (wifiRssi > -127)
  {
    wifiColor = myRED;
    signalDots = 1;
  }
  else
  {
    signalDots = 0;
  }

  // Always redraw WiFi signal indicator
  display.fillRect(50, 0, 24, 4, myBLACK);
  const byte dotSize = 2;
  const byte dotSpacing = 1;
  for (byte dotIndex = 0; dotIndex < 5; dotIndex++)
  {
    byte x = 62 - dotIndex * (dotSize + dotSpacing);
    byte y = 0;
    display.fillRect(x, y, dotSize, dotSize, (dotIndex < signalDots) ? wifiColor : myBLACK);
  }

  return true;
}

// ============================================================================
// MAIN LOOP - Display update and time rendering
// ============================================================================
// This is the frame loop for the LED matrix. It redraws the clock and
// updates the blinking colon, while the background task handles sensors and sync.
void loop()
{
  static bool lastTimeNeedsUpdate = false;

  if (timeNeedsUpdate != lastTimeNeedsUpdate)
  {
    display.clearDisplay(); // clear ONLY on transition
    lastTimeNeedsUpdate = timeNeedsUpdate;
  }

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
    time_t nowTime = getCurrentTime();
    struct tm *timeinfo = localtime(&nowTime);

    // ---- Sanity Check RTC Values ----
    /*if (!rtcSanityCheck())
    {
      // Time corruption detected - attempt recovery
      Serial.println("[TIME_RECOVERY] Starting recovery sequence...");

      if (rtcTimeUpdater())
      {
        Serial.println("[TIME_RECOVERY] Time updated successfully");
        skipNextJumpCheck = true; // Skip jump detection after successful recovery
        delay(500);
      }
      else
      {
        Serial.println("[TIME_RECOVERY] Update failed.");
        delay(1000);
        errorFlag = "TIME CORRUPT";
      }
    }*/

    // Calculate 12-hour format hour
    int hour24 = timeinfo->tm_hour;
    int tempHour = (hour24 == 0) ? 12 : (hour24 > 12) ? hour24 - 12
                                                      : hour24;

    currentDay = timeinfo->tm_mday;

    // ---- Trigger hourly alarm when hour changes ----
    if (hour24 != lastAlarmHour && timeinfo->tm_min == 0 && timeinfo->tm_sec == 0)
    {
      hourlyAlarmTriggered = true;
      lastAlarmHour = hour24;
    }

    // Update WiFi signal display every 5 seconds (only redraws if signal changed)
    if (millis() - lastWiFiUpdateTime >= 5000)
    {
      lastWiFiUpdateTime = millis();
      updateWiFiSignalDisplay();
    }

    char dateString[12];
    sprintf(dateString, "%02d/%02d/%04d", currentDay, (timeinfo->tm_mon + 1), (timeinfo->tm_year + 1900));

    // ---- Update Display Every Minute ----
    if (x || timeinfo->tm_sec == 0)
    {
      x = false;
      bool isPM = (hour24 >= 12);
      bool hasError = errorFlag.length() > 0;
      int displayMonth = (timeinfo->tm_mon + 1);
      int displayYear = (timeinfo->tm_year + 1900);
      int displayMinute = timeinfo->tm_min;
      int displayDayOfWeek = timeinfo->tm_wday;

      // ---- Update Date Block (day/month/year) ----
      if (lastDisplayedDay != currentDay || lastDisplayedMonth != displayMonth || lastDisplayedYear != displayYear)
      {
        lastDisplayedDay = currentDay;
        lastDisplayedMonth = displayMonth;
        lastDisplayedYear = displayYear;
        display.fillRect(0, 4, 64, 13, myBLACK);
        display.setFont(NULL);
        display.setTextColor(myCYAN);
        display.setCursor(3, 10);
        display.print(dateString);
      }

      // ---- Update Time Block (hour/minute/AM-PM) ----
      if (lastDisplayedHour != tempHour || lastDisplayedMinute != displayMinute || lastDisplayedIsPM != isPM)
      {
        lastDisplayedHour = tempHour;
        lastDisplayedMinute = displayMinute;
        lastDisplayedIsPM = isPM;
        display.fillRect(0, 17, 64, 30, myBLACK);
        display.setFont(&FreeSans9pt7b);
        display.setTextSize(1);
        display.setTextColor(myWHITE);
        display.setCursor(0, 37);
        display.print(padNum(tempHour));
        display.setCursor(26, 37);
        display.print(padNum(displayMinute));
        display.setFont(NULL);
        display.print(isPM ? " PM" : " AM");
      }

      // ---- Update Day and Temperature Block ----
      if (lastDisplayedErrorFlag != hasError || lastDisplayedDayOfWeek != displayDayOfWeek ||
          lastDisplayedTempC != tempC || (tempC != -99.0f && (int)(lastDisplayedTempC * 10) != (int)(tempC * 10)))
      {
        lastDisplayedErrorFlag = hasError;
        lastDisplayedDayOfWeek = displayDayOfWeek;
        lastDisplayedTempC = tempC;
        display.fillRect(0, 48, 64, 19, myBLACK);

        if (hasError)
        {
          display.setTextColor(myRED);
          display.setCursor(3, 50);
          display.print(errorFlag);
        }
        else
        {
          display.setTextColor(myMAGENTA);
          display.setCursor(3, 50);
          display.print(daysOfTheWeek[displayDayOfWeek]);

          if (tempC == -99.0f)
          {
            display.setCursor(30, 50);
            display.setTextColor(myRED);
            display.print("WAIT");
          }
          else
          {
            // Select temperature color based on value
            uint16_t tempColor = myYELLOW; // default 29-31°C
            if (tempC >= 34)
            {
              tempColor = myRED; // Red for 32°C and above
            }
            else if (tempC < 29)
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
    }
  }
  else
  {
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
    lastDisplayedDay = 255;
    lastDisplayedMinute = 255;
    lastDisplayedDayOfWeek = 255;
  }
  delay(20);
}

// ============================================================================
// TIME SYNCHRONIZATION - NTP and RTC management
// ============================================================================

bool rtcTimeUpdater()
{
  // Early WiFi validation - check connection status first (WL_CONNECTED = 3)
  int wifiStatus = WiFi.status();
  if (wifiStatus != WL_CONNECTED)
  {
    char buffer[100];
    sprintf(buffer, "[WIFI_ERROR] WiFi not connected. Status: %d (need 3). IP: %s", wifiStatus, WiFi.localIP().toString().c_str());
    Serial.println(buffer);
    return false;
  }

  String ntpServer = "pool.ntp.org";

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

  // Update system time with validated NTP time
  struct timeval tv;
  tv.tv_sec = rawtime; // Unix timestamp in seconds
  tv.tv_usec = 0;      // Microseconds
  settimeofday(&tv, nullptr);

  // Verify the time was set
  struct tm *dt = localtime(&rawtime);
  if ((dt->tm_year + 1900) < 2026)
  {
    Serial.println("Year check failed after time sync. Retrying...");
    return false;
  }

  char successMsg[150];
  sprintf(successMsg, "[SUCCESS] System time synced to: %04d-%02d-%02d %02d:%02d:%02d",
          (dt->tm_year + 1900), (dt->tm_mon + 1), dt->tm_mday, dt->tm_hour, dt->tm_min, dt->tm_sec);
  Serial.println(successMsg);
  return true;
}
