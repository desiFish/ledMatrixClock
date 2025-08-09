// This is how many color levels the display shows - the more the slower the update
//#define PxMATRIX_COLOR_DEPTH 4

// Defines the speed of the SPI bus (reducing this may help if you experience noisy images)
//#define PxMATRIX_SPI_FREQUENCY 20000000

// Creates a second buffer for backround drawing (doubles the required RAM)
//#define PxMATRIX_double_buffer true

#include <PxMatrix.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

// Pins for LED MATRIX
#define P_LAT 22
#define P_A 19
#define P_B 23
#define P_C 18
#define P_D 5
#define P_E 15
#define P_OE 16
hw_timer_t* timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

#define matrix_width 64
#define matrix_height 64

// This defines the 'on' time of the display is us. The larger this number,
// the brighter the display. If too large the ESP will crash
uint8_t display_draw_time = 30;  //30-70 is usually fine

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

uint16_t myCOLORS[8] = { myRED, myGREEN, myBLUE, myWHITE, myYELLOW, myCYAN, myMAGENTA, myBLACK };

void IRAM_ATTR display_updater() {
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  display.display(display_draw_time);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void display_update_enable(bool is_enable) {
  if (is_enable) {
    timer = timerBegin(1000000);
    timerAttachInterrupt(timer, &display_updater);
    timerAlarm(timer, 4000, true, 0);
    timerStart(timer);
  } else {
    timerDetachInterrupt(timer);
    timerStop(timer);
  }
}

void setup() {

  Serial.begin(9600);
  // Define your display layout here, e.g. 1/8 step, and optional SPI pins begin(row_pattern, CLK, MOSI, MISO, SS)
  display.begin(32);

  // Rotate display
  display.setRotate(true);

  // Flip display
  //display.setFlip(true);
  
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

  delay(2000);
  for (int i = 0; i < 3; i++) {
    display.fillRect(0, 0, 64, 64, myCOLORS[i]);
    delay(100);
  }
}
union single_double {
  uint8_t two[2];
  uint16_t one;
} this_single_double;

unsigned long last_draw = 0;
void scroll_text(uint8_t ypos, unsigned long scroll_delay, String text, uint8_t colorR, uint8_t colorG, uint8_t colorB) {
  uint16_t text_length = text.length();
  display.setTextWrap(false);  // we don't wrap text so it scrolls nicely
  display.setTextSize(1);
  display.setRotation(1);
  display.setTextColor(display.color565(colorR, colorG, colorB));

  // Asuming 5 pixel average character width
  for (int xpos = matrix_width; xpos > -(matrix_width + text_length * 3); xpos--) {
    display.setTextColor(display.color565(colorR, colorG, colorB));
    display.clearDisplay();
    display.setCursor(xpos, ypos);
    display.println(text);
    delay(scroll_delay);
    yield();
  }
}

uint8_t icon_index = 0;
void loop() {
  display.clearDisplay();
scroll_text(1, 50, "07/08/2025 WEDNESDAY", 96, 96, 250);
  /*
  display.setFont(NULL);
  display.setTextSize(2);
  display.setTextColor(myCYAN);
  display.setCursor(0, 0); // Y=0
  display.print("38");
  // Draw degree symbol manually for compatibility
  display.drawCircle(26,2,2, myCYAN); // Small circle as degree symbol
   display.setCursor(30, 0);
  display.print("C"); // Height: 16px

  // Time (FreeSans9pt7b, size 1) - moved further down to avoid overlap
  display.setFont(&FreeSans9pt7b);
  display.setTextSize(1);
  display.setTextColor(myWHITE);
  display.setCursor(0, 30); // Y=16+6 spacing
  display.print("22:58"); // Height: ~18px

  // AM/PM (built-in font, size 1), right of time
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(myRED);
  display.setCursor(50, 22); // X=55, Y=22
  display.print("PM"); // Height: 8px

  // Date (built-in font, size 1)
  display.setTextColor(myGREEN);
  display.setCursor(0, 42); // Y=22+18+2 spacing
  display.print("28/12/2025"); // Height: 8px

  // Month (built-in font, size 1)
  display.setTextColor(myYELLOW);
  display.setCursor(0, 52); // Y=42+8+2 spacing
  display.print("SEPTEMBER"); // Height: 8px

  delay(1000);*/
}
