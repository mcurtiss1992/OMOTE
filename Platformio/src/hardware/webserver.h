#ifndef __WEBSERVER_H__
#define __WEBSERVER_H__

#include "commandHandler.h"

#if ENABLE_WIFI_AND_MQTT == 1

#include "WiFi.h"
#include <PubSubClient.h>
#include <WebServer.h>

extern bool setupEnabled;

const char siteHeader[] PROGMEM = "<!DOCTYPE html> <html>\n<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n<title>Test</title>\n<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\nbody{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n.button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n.button-on {background-color: #3498db;}\n.button-on:active {background-color: #2980b9;}\n.button-off {background-color: #34495e;}\n.button-off:active {background-color: #2c3e50;}\np {font-size: 14px;color: #888;margin-bottom: 10px;}\n</style>\n</head>\n<body>\n<h1>OMOTE Setup</h1>\n<h3>Setup pages:</h3>\n<a href=\"/wifi\">Wifi Config</a>\n<a href=\"/devices\">Device Config</a>\n<a href=\"/scenes\">Scene Config</a>\n";

void webserver_setup(void);
void handle_OnConnect(void);
void handle_WiFiSettings(void);
void handle_FinishSetup(void);
void handle_DeviceSettings(void);
void handle_SceneSettings(void);
void handleEditJson(void);
void handleListJsonFiles(void);
void handle_NotFound(void);
void webserverHandleClient(void);
String SendHomepage(void);
String SendWifiPage(void);
String SendDevicePage(void);
String SendScenePage(void);
String SendFinishSetupPage(void);
#endif

#endif /*__MQTT_H__*/
