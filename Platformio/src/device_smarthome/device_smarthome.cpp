#include "commandHandler.h"
#include "device_smarthome/device_smarthome.h"
#include "device_denonAVR/device_denonAVR.h"

void register_device_smarthome() {
  #ifdef ENABLE_KEYBOARD_MQTT
  commands[SMARTHOME_MQTT_CINEMA_STAIRS_ON]             = makeCommandData(MQTT, {"cinema_stair_on"             }); // payload must be set when calling commandHandler
  commands[SMARTHOME_MQTT_CINEMA_STAIRS_OFF]            = makeCommandData(MQTT, {"cinema_stair_off"});                         // payload must be set when calling commandHandler
  commands[SMARTHOME_MQTT_CINEMA_MAIN_ON]               = makeCommandData(MQTT, {"cinema_main_on"             }); // payload must be set when calling commandHandler
  commands[SMARTHOME_MQTT_CINEMA_MAIN_OFF]               = makeCommandData(MQTT, {"cinema_main_off"});                         // payload must be set when calling commandHandler
  commands[SMARTHOME_MQTT_CINEMA_STAIR_BRIGHTNESS_SET]  = makeCommandData(MQTT, {"cinema_stair_setbrightness"   }); // payload must be set when calling commandHandler
  commands[SMARTHOME_MQTT_CINEMA_MAIN_BRIGHTNESS_SET]   = makeCommandData(MQTT, {"cinema_main_setbrightness"   }); // payload must be set when calling commandHandler
  #endif
}
