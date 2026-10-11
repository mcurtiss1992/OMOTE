#pragma once

#include <lvgl.h>

// All devices as tiles (the device modes from devicemodes.json). A tile switches to that device's mode,
// the first tile goes back to the scene that runs. Opened by a swipe down from the top left corner.
const char * const tabName_devices = "Devices";
extern uint16_t GUI_DEVICES_ACTIVATE;
void register_gui_devices(void);
