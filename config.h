/*
  config.h - LED Matrix Display Configuration and Helper Functions

  This file contains all hardware configuration, pin definitions, color definitions,
  and display management functions for the LED Matrix 64x64 counter.
*/

#ifndef CONFIG_H
#define CONFIG_H

#include <PxMatrix.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>
#include "RTClib.h"
#include <SparkFun_TMP117.h> // TMP117 temperature sensor library
#include <7Semi_INA219.h>
#include <BH1750.h>

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

// Color palette array for easy access
uint16_t myCOLORS[8] = {myRED, myGREEN, myBLUE, myWHITE, myYELLOW, myCYAN, myMAGENTA, myBLACK};

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

RTC_DS1307 rtc;

// Day of week names
char daysOfTheWeek[7][12] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// ============================================================================
// NTP CLIENT FOR TIME SYNCHRONIZATION
// ============================================================================

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// ============================================================================
// GLOBAL TIME AND TASK VARIABLES
// ============================================================================

// String variables for date and time formatting
String formattedDate;
String dayStamp;
String timeStamp;

// Task handle for core 0 (dual-core CPU management)
TaskHandle_t loop1Task;

// NTP server configuration
String ntpServer = "";

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

/**
 * Pad a number with leading zero if less than 10
 * @param num The number to pad
 * @return String with padded number (e.g., 5 becomes "05")
 */
String padNum(int num)
{
    return (num < 10 ? "0" : "") + String(num);
}

TMP117 temp117;
INA219_7Semi ina(0x40);  // single device @ 0x40
BH1750 lightMeter(0x23); // Initalize light sensor

const int buzzerPin = 33;
const int switch1Pin = 25;
const int switch2Pin = 26;

unsigned long lastRequestTime = 0;
const unsigned long readInterval = 30000;
bool conversionRequested = false;

unsigned long lastCurrentTime = 0;
const unsigned long currentInterval = 500;

// LUX (BH1750) update frequency
unsigned long lastLightRead = 0;
unsigned long lastBrightnessUpdate = 0;

const unsigned long lightInterval = 4000;    // lux sampling
const unsigned long brightnessInterval = 50; // animation

#endif // CONFIG_H
