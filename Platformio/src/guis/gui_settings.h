#pragma once

#include <lvgl.h>

const char * const tabName_settings = "Settings";
void register_gui_settings(void);
extern bool setupEnabled;
// accessed by "guiStatusUpdate.cpp"
extern lv_obj_t* objBattSettingsVoltage;
extern lv_obj_t* objBattSettingsPercentage;
// shows the address of the web config server while setup mode is enabled. Called every second by "guiStatusUpdate.cpp"
void updateSetupAddressOnGUI();
//extern lv_obj_t* objBattSettingsIscharging;

