#pragma once
#include <Arduino.h>
#include "driver/ledc.h"
#include <lvgl.h>

// This header only declares. Everything that is common to all display drivers is defined in
// displaydriver.cpp, everything that is specific to one driver in displaydriver0_LovyanGFX.cpp,
// displaydriver1_Arduino_GFX_GPIO.cpp or displaydriver2_Arduino_GFX_LCD.cpp. Each of those files
// compiles to nothing unless DISPLAY_DRIVER selects it, so exactly one of them provides
// init_tft(), my_disp_flush(), my_touchpad_read_display_specific(), draw_touchpoint() and
// touchChipResponds_display_specific().

// Pin assignment ---------------------------------------------------------------------------------
// Deliberately defined here and not in a .cpp: as const at file scope these have internal linkage,
// so every file that includes this header gets its own compile time constant. Do not add extern
// declarations for them anywhere - that would give them external linkage and the linker would then
// see one definition per file.
#if(OMOTE_HARDWARE_REV >= 5)
  const uint8_t SDA_GPIO = 20;
  const uint8_t SCL_GPIO = 19;

  const uint8_t LCD_BL_GPIO = 9;
  const uint8_t LCD_EN_GPIO = 38;
  const uint8_t LCD_CS_GPIO = 39;
  const uint8_t LCD_DC_GPIO = 40;
  const uint8_t LCD_WR_GPIO = 41;
  const uint8_t LCD_RD_GPIO = 42;
  const uint8_t LCD_D0_GPIO = 48;
  const uint8_t LCD_D1_GPIO = 47;
  const uint8_t LCD_D2_GPIO = 21;
  const uint8_t LCD_D3_GPIO = 14;
  const uint8_t LCD_D4_GPIO = 13;
  const uint8_t LCD_D5_GPIO = 12;
  const uint8_t LCD_D6_GPIO = 11;
  const uint8_t LCD_D7_GPIO = 10;

  #define LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#else
  const uint8_t SDA_GPIO = 19;
  const uint8_t SCL_GPIO = 22;

  const uint8_t LCD_BL_GPIO = 4;
  const uint8_t LCD_EN_GPIO = 10;
  const uint8_t LCD_CS_GPIO = 5;
  const uint8_t LCD_DC_GPIO = 9;
  const uint8_t LCD_MOSI_GPIO = 23;
  const uint8_t LCD_SCK_GPIO = 18;

  #define LEDC_SPEED_MODE LEDC_HIGH_SPEED_MODE
#endif

// -----------------------
// Ghost touches? The FT6206 decides with the threshold in register 0x80 (ID_G_THGROUP) how much
// signal counts as a touch. The lower the value, the more sensitive it is, and the more likely
// noise - from the panel itself, from the backlight PWM or from the display bus - shows up as a
// touch that nobody made.
// The display drivers treat that register differently:
//   DISPLAY_DRIVER 0 (LovyanGFX)     : never writes it. Measured on a rev5 board, the value after
//                                      power-on is 0, so the controller runs at its most sensitive.
//   DISPLAY_DRIVER 1, 2 (Arduino_GFX): touch.begin() writes it, 128 is the library's default.
// If you see touches that nobody made, define a threshold here. It is then used by every driver,
// so that they behave the same. Higher = less sensitive, the register is 8 bit, so 0..255.
// Start with 128 and go up in steps if it is not enough. If it gets too high, real touches with a
// light finger are lost.
// Undefined = leave the controller as it is with DISPLAY_DRIVER 0 (LovyanGFX), and use the
// Adafruit default of 128 with DISPLAY_DRIVER 1 and 2 (Arduino_GFX).
// #define TOUCH_THRESHOLD 128

// -----------------------
// Colors look like a photographic negative? Not every ILI9341 panel comes up with the controller's
// display inversion in the same state. The panel this firmware was built against is correct
// without inversion; others - BuyDisplay's 2.8" 240x320 module among them - need it switched on.
// Nothing else about the panel changes, so this one switch is the whole difference between them.
// Define this if your display shows a negative image. It is honoured by every driver.
// Careful: this is not the fix if red and blue are swapped but light and dark are correct. That is
// the RGB/BGR order of the panel, not the inversion, and the knob for it is rgb_order in the
// LovyanGFX panel config resp. the "bgr" argument of the Arduino_ILI9341 constructor.
#define DISPLAY_INVERT_COLORS

// Common to all drivers, defined in displaydriver.cpp --------------------------------------------
// Backlight PWM and LCD power, has to run before init_tft().
void setup_tft(void);
// Starts Wire on OMOTE's pins, has to run after init_tft().
void setup_wire(void);
// Allocates the LVGL draw buffer(s) and fills draw_buf.
void init_lvgl_buffer(void);
extern lv_disp_draw_buf_t draw_buf;

// Is my_disp_flush() allowed to return before the transfer is finished? Only with two LVGL draw
// buffers in internal DMA capable RAM, and only with a driver that can send asynchronously.
// init_lvgl_buffer() sets this, and it stays false in every other case.
extern bool lvgl_flush_async;

// Ask the touch controller whether it is there. Set once the answer was yes.
extern bool TouchInitSuccessful;
bool touchChipResponds(void);

#if ((DISPLAY_DRIVER == 0) || (DISPLAY_DRIVER == 2)) && (LV_COLOR_16_SWAP == 0)
// Swaps the two bytes of every RGB565 pixel in place, see displaydriver.cpp.
void swap_rgb565_inplace(lv_color_t *pixels, uint32_t count);
#endif

// Provided by the selected displaydriver<n>_*.cpp ------------------------------------------------
void init_tft(void);
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void my_touchpad_read_display_specific(lv_indev_drv_t *indev_driver, lv_indev_data_t *data);
void draw_touchpoint(uint16_t x, uint16_t y);
bool touchChipResponds_display_specific(void);
