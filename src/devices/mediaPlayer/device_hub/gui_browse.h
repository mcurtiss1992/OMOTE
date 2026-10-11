#pragma once

#include <lvgl.h>

// Shows the hub's menu for the active scene (see device_hub.h). Put "Browse" first in a scene's GUI list.
const char * const tabName_browse = "Browse";
void register_gui_browse(void);

// used by device_hub.cpp when a new page or the online state arrived via MQTT
void browse_gui_update(void);
