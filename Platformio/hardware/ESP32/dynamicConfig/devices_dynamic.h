#ifndef DYNAMIC_DEVICE_REGISTRATION_H
#define DYNAMIC_DEVICE_REGISTRATION_H

#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "applicationInternal/commandHandler.h"
#include <cstring>  // For memset

// --- Configuration Constants ---
#define FORMAT_SPIFFS_IF_FAILED true
#define MAX_COMMANDS 200   // Limit to 200 commands
#define MAX_NAME_LEN 32    // Maximum length for a command name

// --- Command Table Structure ---
struct CommandEntry {
  char name[MAX_NAME_LEN]; // Command name (fixed size)
  uint16_t value;          // Command value (unique ID, etc.)
  bool inUse;              // True if the slot is occupied
};

// Preallocated table for command entries (defined in the .cpp file)
extern CommandEntry commandTable[MAX_COMMANDS];
// Global counter for assigning a unique value to each command (defined in the .cpp file)
extern uint16_t nextCommandValue;

// --- Function Prototypes ---

/**
 * @brief Computes the hash index for a given string using the DJB2 algorithm.
 *
 * @param str The input string.
 * @return The computed hash index modulo MAX_COMMANDS.
 */
uint8_t hashIndex(const char* str);

/**
 * @brief Registers a command into the fixed command table using linear probing.
 *
 * This function saves the command data into the preallocated table and calls your
 * register_command() function (provided in your command handler) based on the command type.
 *
 * @param name The command name.
 * @param commandType The type of command ("MQTT", "IR", "BLE").
 * @param commandData The primary command data.
 * @param commandDataExtended The extended command data.
 */
void register_command_dynamic(const char* name, const char* commandType, const char* commandData, const char* commandDataExtended);

/**
 * @brief Reads file content from SPIFFS.
 *
 * @param fs The filesystem object.
 * @param path The file path.
 * @return A String containing the file content.
 */
String readFileDevices(fs::FS &fs, const char *path);

/**
 * @brief Reads a device's JSON file and registers each command dynamically.
 *
 * The function reads a JSON file named in the format "/device_<deviceName>.json", parses
 * the contained JSON array, and registers each command found.
 *
 * @param deviceName The name of the device.
 * @return true if the device commands were registered successfully, false otherwise.
 */
boolean register_dynamic_device(const char *deviceName);

/**
 * @brief Clears the command table.
 *
 * This function resets all entries in the command table to a default state.
 */
void clearCommands();

/**
 * @brief Reads a master devices file and registers each dynamic device.
 *
 * The master file (typically "/devices.json") is expected to be a JSON array of device names.
 */
void register_dynamic_devices();

/**
 * @brief Looks up a command in the command table by name.
 *
 * This function demonstrates how to search for a command using the same hash function.
 *
 * @param lookupName The name of the command to look up.
 */
void lookup_command(const char* lookupName);

#endif // DYNAMIC_DEVICE_REGISTRATION_H
