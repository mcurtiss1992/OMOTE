#include <map>
#include "scenes/scene_appleTV.h"
#include "applicationInternal/keys.h"
#include "applicationInternal/scenes/sceneRegistry.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
// devices
#include "devices/TV/device_samsungTV/device_samsungTV.h"
#include "devices/AVreceiver/device_yamahaAmp/device_yamahaAmp.h"
#include "devices/AVreceiver/device_denonAvr/device_denonAvr.h"
#include "applicationInternal/commandHandler.h"
// guis
#include "devices/mediaPlayer/device_appleTV/gui_appleTV.h"

uint16_t SCENE_APPLETV      ; //"Scene_appleTV"
uint16_t SCENE_APPLETV_FORCE; //"Scene_appleTV_force"

std::map<char, repeatModes> key_repeatModes_appleTV;
std::map<char, uint16_t> key_commands_short_appleTV;
std::map<char, uint16_t> key_commands_long_appleTV;

void scene_setKeys_appleTV() {
  key_repeatModes_appleTV = {
  
  
  
  
  
  
  
  
  
  
  };
  
  key_commands_short_appleTV = {
  
    /*{KEY_OFF,   SCENE_ALLOFF_FORCE},*/
    /*{KEY_STOP,  GUI_SMARTHOME_ACTIVATE},*/ {KEY_REWI,  KEYBOARD_BLE_REWIND  }, {KEY_PLAY,  KEYBOARD_BLE_PLAYPAUSE  },    {KEY_FORW,  KEYBOARD_BLE_FASTFORWARD  },
  {KEY_CONF,  KEYBOARD_BLE_MENU  },                                                                          {KEY_INFO,  KEYBOARD_BLE_HOME  },
                                                       {KEY_UP,    KEYBOARD_BLE_UP  },
                      {KEY_LEFT,  KEYBOARD_BLE_LEFT  },         {KEY_OK,    KEYBOARD_BLE_SELECT  },  {KEY_RIGHT, KEYBOARD_BLE_RIGHT  },
                                                       {KEY_DOWN,  KEYBOARD_BLE_DOWN  },
    {KEY_BACK,  KEYBOARD_BLE_BACK  },                                                                        /*{KEY_SRC,   COMMAND_UNKNOWN  },*/
    {KEY_VOLUP, DENONAVR_MQTT_VOL_UP  },                      {KEY_MUTE,  DENONAVR_MQTT_MUTE},                  /*{KEY_CHUP,  COMMAND_UNKNOWN  },*/


    {KEY_VOLDO, DENONAVR_MQTT_VOL_DOWN },                      /*{KEY_REC,   SCENE_BACK_TO_PREVIOUS_GUI_LIST  },   {KEY_CHDOW, COMMAND_UNKNOWN  },*/


    /* {KEY_RED,   SCENE_TV_FORCE   },    {KEY_GREEN, SCENE_FIRETV_FORCE},  {KEY_YELLO, SCENE_CHROMECAST_FORCE},{KEY_BLUE,  SCENE_APPLETV_FORCE},*/
  
  
  };
  
  key_commands_long_appleTV = {
  
  
  };

}

void scene_start_sequence_appleTV(void) {
  //executeCommand(SAMSUNG_POWER_ON);
  delay(500);
  executeCommand(DENONAVR_MQTT_ON);
  delay(1500);
  executeCommand(DENONAVR_MQTT_INPUT_SHIELD);
  delay(3000);
  //executeCommand(SAMSUNG_INPUT_HDMI_3);

}

void scene_end_sequence_appleTV(void) {

}

std::string scene_name_appleTV = "Apple TV";
t_gui_list scene_appleTV_gui_list = {tabName_appleTV};

void register_scene_appleTV(void) {
  register_command(&SCENE_APPLETV,       makeCommandData(SCENE, {scene_name_appleTV}));
  register_command(&SCENE_APPLETV_FORCE, makeCommandData(SCENE, {scene_name_appleTV, "FORCE"}));

  register_scene(
    scene_name_appleTV,
    & scene_setKeys_appleTV,
    & scene_start_sequence_appleTV,
    & scene_end_sequence_appleTV,
    & key_repeatModes_appleTV,
    & key_commands_short_appleTV,
    & key_commands_long_appleTV,
    & scene_appleTV_gui_list,
    SCENE_APPLETV);
}
