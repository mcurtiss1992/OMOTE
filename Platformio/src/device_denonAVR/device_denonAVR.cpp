#include "commandHandler.h"
#include "device_denonAVR/device_denonAVR.h"

void register_device_denonavr()
{
#ifdef ENABLE_KEYBOARD_MQTT
    commands[DENONAVR_MQTT_ON] = makeCommandData(MQTT, {"omote/denonavr", "avr_on"});
    commands[DENONAVR_MQTT_OFF] = makeCommandData(MQTT, {"omote/denonavr", "avr_off"});
    commands[DENONAVR_MQTT_MUTE] = makeCommandData(MQTT, {"omote/denonavr", "avr_mute"});
    commands[DENONAVR_MQTT_VOL_UP] = makeCommandData(MQTT, {"omote/denonavr", "avr_vol_up"});
    commands[DENONAVR_MQTT_VOL_DOWN] = makeCommandData(MQTT, {"omote/enonavr", "avr_vol_down"});
    commands[DENONAVR_MQTT_INPUT_BRAY] = makeCommandData(MQTT, {"omote/denonavr", "avr_input_bray"});
    commands[DENONAVR_MQTT_INPUT_PS5] = makeCommandData(MQTT, {"omote/denonavr", "avr_input_ps5"});
    // commands[DENONAVR_MQTT_INPUT_SWITCH] = makeCommandData(MQTT, {"omote/denonavr", "avr_input_switch"});
    // commands[DENONAVR_MQTT_INPUT_SHIELD] = makeCommandData(MQTT, {"omote/denonavr", "avr_input_shield"});
    // commands[DENONAVR_MQTT_SOUND_MODE_MOVIE] = makeCommandData(MQTT, {"omote/denonavr", "avr_sound_mode_movie"});

#endif
}
