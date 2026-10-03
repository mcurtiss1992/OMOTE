#pragma once

#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__

#if ENABLE_WIFI_AND_MQTT == 1

#include <string>

// mDNS name of the remote: the config app can reach it as http://omote.local
#define OMOTE_HOSTNAME "omote"

// Registers the routes and starts listening. Does not wait for WiFi.
void init_webserver_HAL(void);
// Serves pending requests. Only called while setup mode is enabled.
void webserver_handleClient_HAL(void);
// True (once) if this boot is a restart that the config app requested via /restart.
// The remote then comes back up in setup mode, so the app can keep talking to it.
bool webserver_consumeRestartIntoSetup_HAL(void);
// Address to show on the remote, e.g. "omote.local / 192.168.1.23". Empty while WiFi is not connected.
std::string webserver_getAddress_HAL(void);
// /sendCommand executes a configured command ("Test" buttons of the config app). Returns false if it does not exist.
typedef bool (*tSendConfiguredCommand_cb)(std::string device, std::string command, std::string payload);
void set_sendConfiguredCommand_cb_HAL(tSendConfiguredCommand_cb pSendConfiguredCommand_cb);

#endif

#endif /*__WEBSERVER_H__*/
