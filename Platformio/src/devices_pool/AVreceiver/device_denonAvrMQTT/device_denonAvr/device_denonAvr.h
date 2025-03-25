#pragma once

// Only activate the commands that are used. Every command takes 100 bytes, wether used or not.
// extern uint16_t DENON_POWER_TOGGLE;
// extern uint16_t DENON_POWER_SLEEP;
// extern uint16_t DENON_VOL_MINUS;
// extern uint16_t DENON_VOL_PLUS;
// extern uint16_t DENON_VOL_MUTE;
// extern uint16_t DENON_CHAN_PLUS;
// extern uint16_t DENON_CHAN_MINUS;
// extern uint16_t DENON_INPUT_CABLESAT;
// extern uint16_t DENON_INPUT_MEDIAPLAYER;
// extern uint16_t DENON_INPUT_BLURAY;
// extern uint16_t DENON_INPUT_GAME;
// extern uint16_t DENON_INPUT_AUX1;
// extern uint16_t DENON_INPUT_AUX2;
// extern uint16_t DENON_INPUT_PHONO;
// extern uint16_t DENON_INPUT_TUNER;
// extern uint16_t DENON_INPUT_TV;
// extern uint16_t DENON_INPUT_USB;
// extern uint16_t DENON_INPUT_BLUETOOTH;
// extern uint16_t DENON_INPUT_INTERNET;
// extern uint16_t DENON_INPUT_HEOS;
// extern uint16_t DENON_POWER_ECO;
// extern uint16_t DENON_INFO;
// extern uint16_t DENON_OPTION;
// extern uint16_t DENON_BACK;
// extern uint16_t DENON_SETUP;
// extern uint16_t DENON_MENU_ENTER;
// extern uint16_t DENON_MENU_UP;
// extern uint16_t DENON_MENU_LEFT;
// extern uint16_t DENON_MENU_RIGHT;
// extern uint16_t DENON_MENU_DOWN;
// extern uint16_t DENON_SOUNDMODE_MOVIE;
// extern uint16_t DENON_SOUNDMODE_MUSIC;
// extern uint16_t DENON_SOUNDMODE_GAME;
// extern uint16_t DENON_SOUNDMODE_PURE;
// extern uint16_t DENON_QUICKSELECT_1;
// extern uint16_t DENON_QUICKSELECT_2;
// extern uint16_t DENON_QUICKSELECT_3;
// extern uint16_t DENON_QUICKSELECT_4;
// extern uint16_t DENON_MEDIA_PREV;
// extern uint16_t DENON_MEDIA_PLAYPAUSE;
// extern uint16_t DENON_MEDIA_NEXT;



extern uint16_t DENONAVR_MQTT_ON ;   //"DENONAVR_MQTT_ON"
extern uint16_t DENONAVR_MQTT_OFF ;   //"DENONAVR_MQTT_OFF"
extern uint16_t DENONAVR_MQTT_MUTE ;   //"DENONAVR_MQTT_MUTE"
extern uint16_t DENONAVR_MQTT_VOL_UP ;   //"DENONAVR_MQTT_VOL_UP"
extern uint16_t DENONAVR_MQTT_VOL_DOWN ;   //"DENONAVR_MQTT_VOL_DOWN"
extern uint16_t DENONAVR_MQTT_INPUT_BRAY ;   //"DENONAVR_MQTT_INPUT_BRAY"
extern uint16_t DENONAVR_MQTT_INPUT_PS5 ;   //"DENONAVR_MQTT_INPUT_PS5"
extern uint16_t DENONAVR_MQTT_INPUT_SWITCH ;   //"DENONAVR_MQTT_INPUT_SWITCH"
extern uint16_t DENONAVR_MQTT_INPUT_SHIELD ;   //"DENONAVR_MQTT_INPUT_SHIELD"
extern uint16_t DENONAVR_MQTT_SOUND_MODE_MOVIE ;   //"DENONAVR_MQTT_SOUND_MODE_MOVIE"

void register_device_denonAvr();
