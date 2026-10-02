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
#include <cstring>     // For memset
#include <string>      // For std::string
#include "applicationInternal/omote_log.h"

#define FORMAT_SPIFFS_IF_FAILED true

// ----- Fixed Memory Block Setup -----
#define MAX_COMMANDS 200   // Limit to 200 commands
#define MAX_NAME_LEN 32    // Maximum length for a command name

struct CommandEntry {
  char name[MAX_NAME_LEN]; // Command name (fixed size)
  uint16_t value;          // Command value (unique ID, etc.)
  bool inUse;              // True if the slot is occupied
};

// Preallocated table for command entries
CommandEntry commandTable[MAX_COMMANDS];
// Global counter for assigning a unique value to each command
uint16_t nextCommandValue = 0;

// ----- Hash Function & Registration -----
// Simple DJB2 hash to compute an index from the command name.
uint8_t hashIndex(const char* str) {
  uint32_t hash = 5381;
  while (*str) {
    hash = ((hash << 5) + hash) + (uint8_t)(*str); // hash * 33 + current char
    str++;
  }
  return hash % MAX_COMMANDS;
}

// Register a command into the fixed table using linear probing.
void register_command_dynamic(const char* name, const char* commandType, const char* commandData, const char* commandDataExtended) {
  uint8_t index = hashIndex(name);
  uint8_t originalIndex = index;

  // Linear probing to resolve collisions.
  while (commandTable[index].inUse && strcmp(commandTable[index].name, name) != 0) {
    index = (index + 1) % MAX_COMMANDS;
    if (index == originalIndex) {
      omote_log_e("Error: Command table full!");
      return;
    }
  }

  // Save the command name into the table.
  strncpy(commandTable[index].name, name, MAX_NAME_LEN);
  commandTable[index].name[MAX_NAME_LEN - 1] = '\0'; // Ensure null termination

  // Use a local variable for the command ID.
  uint16_t cmdId = 0;
  if (strcmp(commandType, "MQTT") == 0) {
    register_command(&cmdId, makeCommandData(MQTT, {commandData, commandDataExtended}));
  } else if (strcmp(commandType, "IR") == 0) {
    int irProtocol = std::atoi(commandData);
    register_command(&cmdId, makeCommandData(IR, {irProtocol, commandDataExtended}));
  } else if (strcmp(commandType, "BLE") == 0) {
    register_command(&cmdId, makeCommandData(BLE_KEYBOARD, {commandData, commandDataExtended}));
  }

  commandTable[index].value = cmdId;
  commandTable[index].inUse = true;

  // Debug output
  omote_log_i("Registered command: %s at index %d with value %d", name, index, commandTable[index].value);
}

// ----- File Reading Helper -----
// Reads file content from SPIFFS.
String readFileDevices(fs::FS &fs, const char *path) {
  String outContent = "";
  omote_log_i("Reading file: %s", path);
  
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    omote_log_e("- failed to open file for reading");
    return "- failed to open file for reading";
  }
  
  size_t fileSize = file.size();
  std::unique_ptr<char[]> buf(new char[fileSize + 1]);
  file.readBytes(buf.get(), fileSize);
  buf[fileSize] = '\0'; // Null-terminate
  outContent = String(buf.get());
  file.close();
  
  omote_log_i("%s", outContent.c_str());
  return outContent;
}

// ----- Dynamic Device Registration -----
// Reads a device's JSON file and registers each command dynamically.
boolean register_dynamic_device(const char *deviceName) {
  String deviceFilePath = String("/device_") + deviceName + ".json";
  String deviceFileContent;
  
  if (SPIFFS.exists(deviceFilePath.c_str())) {
    deviceFileContent = readFileDevices(SPIFFS, deviceFilePath.c_str());
  } else {
    deviceFileContent = "[]"; // Default to an empty JSON array if not found
  }

  // Create a JsonDocument with an appropriate capacity.
  JsonDocument device;
  DeserializationError error = deserializeJson(device, deviceFileContent);
  deviceFileContent.clear();

  if (error) {
    omote_log_e("Failed to parse JSON:");
    omote_log_e("%s", error.f_str());
    return false;
  }

  JsonArray commandArray = device.as<JsonArray>();
  if (!commandArray) {
    omote_log_e("JSON is not an array!");
    return false;
  }

  // Iterate over each command object in the JSON array.
  for (JsonObject command : commandArray) {
    std::string strname = deviceName;
    std::string com = command["name"].as<const char*>();
    std::string finalName = strname + "_" + com;
    const char* name = finalName.c_str();
    const char* commandType = command["commandType"];
    const char* commandData = command["commandData"];
    const char* commandDataExtended = command["commandDataExtended"];

    omote_log_i("Registering command: %s", name ? name : "N/A");
    if (name && commandType && commandData && commandDataExtended) {
      register_command_dynamic(name, commandType, commandData, commandDataExtended);
    }
  }
  commandArray.clear();
  device.clear();
  return true;
}

void clearCommands() {
  // Clear out the commandTable to prepare for clean data.
  for (int i = 0; i < MAX_COMMANDS; ++i) {
    std::memset(commandTable[i].name, 0, MAX_NAME_LEN);  // Clear the name
    commandTable[i].value = 0;                           // Reset the value
    commandTable[i].inUse = false;                       // Mark as not in use
  }
}

// Reads a master devices file and registers each dynamic device.
void register_dynamic_devices() {
  Serial.begin(115200);
  delay(100);
  
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    omote_log_e("SPIFFS Mount Failed");
    return;
  }
  
  clearCommands();
  nextCommandValue = getComID();
  
  String devicesFileContent = SPIFFS.exists("/devices.json") ? readFileDevices(SPIFFS, "/devices.json") : "[]";

  // Create a JsonDocument with an appropriate capacity.
  JsonDocument devices;
  DeserializationError error = deserializeJson(devices, devicesFileContent);
  devicesFileContent.clear();

  if (error) {
    Serial.print("Failed to parse JSON: ");
    omote_log_e("%s", error.f_str());
    return;
  }

  JsonArray deviceArray = devices.as<JsonArray>();
  if (!deviceArray) {
    omote_log_e("devices.json is not an array!");
    return;
  }

  // For each device name in devices.json, register its commands.
  for (JsonVariant value : deviceArray) {
    const char* deviceName = value.as<const char*>();
    omote_log_i("Registering device: %s", deviceName);
    register_dynamic_device(deviceName);
  }
  
  register_keyboardCommands();
  deviceArray.clear();
}

// ----- Example: Lookup for a Command -----
// Demonstrates how to locate a command using the same hash.
void lookup_command(const char* lookupName) {
  uint8_t index = hashIndex(lookupName);
  uint8_t originalIndex = index;
  bool found = false;
  
  while (commandTable[index].inUse) {
    if (strcmp(commandTable[index].name, lookupName) == 0) {
      omote_log_i("Lookup for %s found value: %d", lookupName, commandTable[index].value);
      found = true;
      break;
    }
    index = (index + 1) % MAX_COMMANDS;
    if (index == originalIndex) break; // Searched entire table
  }
  
  if (!found) {
    omote_log_i("Command %s not found.", lookupName);
  }
}
