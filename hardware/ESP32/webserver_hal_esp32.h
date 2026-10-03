#pragma once

#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__

#if ENABLE_WIFI_AND_MQTT == 1

#include <string>

// mDNS name of the remote: the config app can reach it as http://omote.local
#define OMOTE_HOSTNAME "omote"

// Registers the routes and starts listening. Does not wait for WiFi.
void webserver_setup(void);
// Serves pending requests. Only called while setup mode is enabled.
void webserverHandleClient(void);
// True (once) if this boot is a restart that the config app requested via /restart.
// The remote then comes back up in setup mode, so the app can keep talking to it.
bool webserver_consumeRestartIntoSetup(void);
// Address to show on the remote, e.g. "omote.local / 192.168.1.23". Empty while WiFi is not connected.
std::string webserver_getAddress(void);

#endif

#endif /*__WEBSERVER_H__*/
