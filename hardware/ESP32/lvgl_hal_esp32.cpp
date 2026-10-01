#include <lvgl.h>
#include <esp_heap_caps.h>
#include <soc/soc_memory_layout.h> // esp_ptr_internal(), to report where the draw buffers ended up
#include "tft_hal_esp32.h"
#include "sleep_hal_esp32.h"
#include "displaydriver.h"

// Read the touchpad
void my_touchpad_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data) {
  // Wait until the touch controller answers. After a soft restart (ESP.restart()) it is not
  // power cycled and can need a moment. With DISPLAY_DRIVER 1 and 2 this is also what initializes
  // the touch, because touchChipResponds() calls touch.begin().
  // Retry at most every 500 ms, not on every LVGL poll: with a missing or broken touch
  // controller this would otherwise run every 30 ms forever, and with DISPLAY_DRIVER 1 and 2 each
  // attempt deletes and allocates an Adafruit_I2CDevice.
  if (!TouchInitSuccessful) {
    static unsigned long lastTouchRetry = 0;
    if (lastTouchRetry == 0 || millis() - lastTouchRetry >= 500) {
      lastTouchRetry = millis();
      TouchInitSuccessful = touchChipResponds();
    }
    if (!TouchInitSuccessful) {
      data->state = LV_INDEV_STATE_REL;
      return;
    }
  }
  
  my_touchpad_read_display_specific(indev_driver, data);

  if (data->state == LV_INDEV_STATE_PR) {
    setLastActivityTimestamp_HAL();
  }
}

void init_lvgl_HAL() {
  // first init TFT -----------------------------------------------------------------------------------------
  setup_tft();
  init_tft();

  setup_wire();

  // allocate lvgl buffers ----------------------------------------------------------------------------------
  init_lvgl_buffer();

  // Initialize the display driver --------------------------------------------------------------------------
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init( &disp_drv );
  disp_drv.hor_res = SCR_WIDTH;
  disp_drv.ver_res = SCR_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register( &disp_drv );

  // Initialize the touchscreen driver ----------------------------------------------------------------------
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init( &indev_drv );
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register( &indev_drv );

}
