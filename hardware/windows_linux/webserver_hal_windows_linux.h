#pragma once

#if (ENABLE_WIFI_AND_MQTT == 1)

#include <string>

/*
  Simulated web server for the OMOTE Config app, with the same API as the remote
  (see "hardware/ESP32/webserver_hal_esp32.cpp"). It listens on http://localhost:8081
  (or the port in the environment variable OMOTE_HTTP_PORT) and, like on the remote,
  only answers while setup mode ("Web config" in the settings) is enabled.
*/

void init_webserver_HAL(void);
void webserver_handleClient_HAL(void);
bool webserver_consumeRestartIntoSetup_HAL(void);
std::string webserver_getAddress_HAL(void);
typedef bool (*tSendConfiguredCommand_cb)(std::string device, std::string command, std::string payload);
void set_sendConfiguredCommand_cb_HAL(tSendConfiguredCommand_cb pSendConfiguredCommand_cb);

#endif
