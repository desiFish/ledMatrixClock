/*
  config.h - Shared hardware setup and helper objects

  This file keeps the display wiring, shared sensor objects, and global state
  together so the main sketch can stay focused on the clock behavior.

  Put things here when they are part of the hardware or shared state,
  not when they belong to the main program flow.
*/

#ifndef CONFIG_H
#define CONFIG_H

#include <PxMatrix.h>
#include <Fonts/FreeSans9pt7b.h>

#include <time.h>
#include <sys/time.h>
#include <SparkFun_TMP117.h> // TMP117 temperature sensor library
#include <7Semi_INA219.h>
#include <BH1750.h>

#include <WiFi.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

// ============================================================================
// DISPLAY PIN CONFIGURATION
// ============================================================================

#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 16

// ============================================================================
// DISPLAY HARDWARE SETUP
// ============================================================================

hw_timer_t *timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

#define matrix_width 64
#define matrix_height 64

// Display brightness: 30-70µs is usually fine for visible quality
// Adjust based on your power supply and desired brightness
uint8_t display_draw_time = 30;

// Matrix display instance
PxMATRIX display(64, 64, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);

// ============================================================================
// COLOR DEFINITIONS
// ============================================================================

uint16_t myRED = display.color565(255, 0, 0);
uint16_t myGREEN = display.color565(0, 255, 0);
uint16_t myBLUE = display.color565(0, 0, 255);
uint16_t myWHITE = display.color565(255, 255, 255);
uint16_t myYELLOW = display.color565(255, 255, 0);
uint16_t myCYAN = display.color565(0, 255, 255);
uint16_t myMAGENTA = display.color565(255, 0, 255);
uint16_t myBLACK = display.color565(0, 0, 0);

// Additional colors for variety
uint16_t myORANGE = display.color565(255, 165, 0);      // Orange
uint16_t myLIME = display.color565(0, 255, 0);          // Lime Green (bright green)
uint16_t myPURPLE = display.color565(128, 0, 128);      // Purple
uint16_t myPINK = display.color565(255, 105, 180);      // Hot Pink
uint16_t myTURQUOISE = display.color565(64, 224, 208);  // Turquoise
uint16_t myGOLD = display.color565(255, 215, 0);        // Gold
uint16_t mySALMON = display.color565(250, 128, 114);    // Salmon
uint16_t myLIGHTBLUE = display.color565(173, 216, 230); // Light Blue

// Color palette array for easy access
uint16_t myCOLORS[16] = {myRED, myGREEN, myBLUE, myWHITE, myYELLOW, myCYAN, myMAGENTA, myBLACK,
                         myORANGE, myLIME, myPURPLE, myPINK, myTURQUOISE, myGOLD, mySALMON, myLIGHTBLUE};

// ============================================================================
// DISPLAY UPDATE INTERRUPT HANDLER
// ============================================================================

/**
 * ISR for display refresh timer
 * Called every 4ms to update the LED matrix display
 */
void IRAM_ATTR display_updater()
{
    portENTER_CRITICAL_ISR(&timerMux);
    display.display(display_draw_time);
    portEXIT_CRITICAL_ISR(&timerMux);
}

/**
 * Enable or disable display refresh timer
 * @param is_enable true to start timer, false to stop
 */
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

// ============================================================================
// RTC AND TIME MANAGEMENT
// ============================================================================

// Using ESP32 internal RTC with NTP synchronization

// Day of week names
char daysOfTheWeek[7][12] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// ============================================================================
// GLOBAL TIME AND TASK VARIABLES
// ============================================================================

// String variables for date and time formatting
String formattedDate;
String dayStamp;
String timeStamp;

// Task handle for core 0 (dual-core CPU management)
TaskHandle_t loop1Task;

// ============================================================================
// APPLICATION STATE
// ============================================================================

// Software version for beta builds
#define SW_VERSION "1.1.0-beta"

// Flags and counters that track application state across the sketch
bool timeNeedsUpdate = false;
String errorFlag = "";
byte currentDay = 0;
byte lastCheckedDay = 0;
unsigned long lastBackgroundNtpTime = 0;  // Track last background NTP sync time
bool x = true; // Used to force display refresh when time sync status changes

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Pad a number with a leading zero when it is less than 10.
 * This makes time and date strings look consistent.
 */
String padNum(int num)
{
    return (num < 10 ? "0" : "") + String(num);
}

/**
 * Get current time from ESP32 internal RTC
 * @return time_t (Unix timestamp)
 */
inline time_t getCurrentTime()
{
    return time(nullptr);
}

/**
 * Get current time broken down into components
 * @return struct tm with year, month, day, hour, minute, second
 */
inline struct tm *getTimeInfo()
{
    time_t now = getCurrentTime();
    return localtime(&now);
}

/**
 * Helper to get hour (0-23)
 */
inline uint8_t getHour()
{
    return getTimeInfo()->tm_hour;
}

/**
 * Helper to get minute (0-59)
 */
inline uint8_t getMinute()
{
    return getTimeInfo()->tm_min;
}

/**
 * Helper to get second (0-59)
 */
inline uint8_t getSecond()
{
    return getTimeInfo()->tm_sec;
}

/**
 * Helper to get day of month (1-31)
 */
inline uint8_t getDay()
{
    return getTimeInfo()->tm_mday;
}

/**
 * Helper to get month (1-12)
 */
inline uint8_t getMonth()
{
    return getTimeInfo()->tm_mon + 1;
}

/**
 * Helper to get year (e.g., 2026)
 */
inline uint16_t getYear()
{
    return getTimeInfo()->tm_year + 1900;
}

/**
 * Helper to get day of week (0=Sunday, 6=Saturday)
 */
inline uint8_t getDayOfWeek()
{
    return getTimeInfo()->tm_wday;
}

// ============================================================================// SENSOR AND ACTUATOR OBJECTS
// ============================================================================

TMP117 temp117;
INA219_7Semi ina(0x40);  // power/current sensor at I2C address 0x40
BH1750 lightMeter(0x23); // light sensor at I2C address 0x23

const int buzzerPin = 33;
const int switch1Pin = 25;
const int switch2Pin = 26;

unsigned long lastRequestTime = 0;
const unsigned long readInterval = 60000;
bool conversionRequested = false;

unsigned long lastCurrentTime = 0;
const unsigned long currentInterval = 5000;

// LUX (BH1750) update frequency
unsigned long lastLightRead = 0;
unsigned long lastBrightnessUpdate = 0;

const unsigned long lightInterval = 4000;    // lux sampling
const unsigned long brightnessInterval = 50; // animation

// NTP related
WiFiUDP ntpUDP;                                         // Create a UDP instance to send and receive NTP packets
NTPClient timeClient(ntpUDP, "in.pool.ntp.org", 19800); // 19800 is offset of India, in.pool.ntp.org is close to India

#endif // CONFIG_H
