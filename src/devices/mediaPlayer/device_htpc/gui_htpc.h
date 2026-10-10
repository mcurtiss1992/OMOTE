#pragma once

#include <lvgl.h>

const char * const tabName_htpc = "Movies";
extern uint16_t GUI_HTPC_ACTIVATE;
void register_gui_htpc(void);

// used by device_htpc.cpp when new data arrived via MQTT
void htpc_gui_update(void);
void htpc_gui_playingChanged(bool playing);
