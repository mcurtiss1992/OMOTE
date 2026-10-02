#include "applicationInternal/commandHandler.h"
#include "device_smarthome.h"

uint16_t SMARTHOME_MQTT_CINEMA_STAIRS_ON;
uint16_t SMARTHOME_MQTT_CINEMA_STAIRS_OFF;
uint16_t SMARTHOME_MQTT_CINEMA_MAIN_ON;
uint16_t SMARTHOME_MQTT_CINEMA_MAIN_OFF;
uint16_t SMARTHOME_MQTT_CINEMA_STAIR_BRIGHTNESS_SET;
uint16_t SMARTHOME_MQTT_CINEMA_MAIN_BRIGHTNESS_SET;

void register_device_smarthome()
{
#if (ENABLE_WIFI_AND_MQTT == 1)
  register_command(&SMARTHOME_MQTT_CINEMA_STAIRS_ON, makeCommandData(MQTT, {"cinema_stair_on"}));                       // payload must be set when calling commandHandler
  register_command(&SMARTHOME_MQTT_CINEMA_STAIRS_OFF, makeCommandData(MQTT, {"cinema_stair_off"}));                     // payload must be set when calling commandHandler
  register_command(&SMARTHOME_MQTT_CINEMA_MAIN_ON, makeCommandData(MQTT, {"cinema_main_on"}));                          // payload must be set when calling commandHandler
  register_command(&SMARTHOME_MQTT_CINEMA_MAIN_OFF, makeCommandData(MQTT, {"cinema_main_off"}));                        // payload must be set when calling commandHandler
  register_command(&SMARTHOME_MQTT_CINEMA_STAIR_BRIGHTNESS_SET, makeCommandData(MQTT, {"cinema_stair_setbrightness"})); // payload must be set when calling commandHandler
  register_command(&SMARTHOME_MQTT_CINEMA_MAIN_BRIGHTNESS_SET, makeCommandData(MQTT, {"cinema_main_setbrightness"}));   // payload must be set when calling commandHandler

#endif
}
