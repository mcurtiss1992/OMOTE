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
#include <cstring> // For memset

#define FORMAT_SPIFFS_IF_FAILED true

// ----- Fixed Memory Block Setup -----
#define MAX_COMMANDS 200   // Limit to 300 commands
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
      Serial.println("Error: Command table full!");
      return;
    }
  }

  // Save the command information into the table.
  strncpy(commandTable[index].name, name, MAX_NAME_LEN);
  commandTable[index].name[MAX_NAME_LEN - 1] = '\0'; // Ensure null termination
  commandTable[index].value = nextCommandValue++;  // Assign a unique value
  commandTable[index].inUse = true;
  if(commandType == "MQTT"){
    register_command(&nextCommandValue, makeCommandData(MQTT, {commandData, commandDataExtended}));
  } else if(commandType == "IR"){
    register_command(&nextCommandValue, makeCommandData(IR, {commandData, commandDataExtended}));
  } else if(commandType == "BLE"){
    register_command(&nextCommandValue, makeCommandData(BLE_KEYBOARD, {commandData, commandDataExtended}));
  }
  // Debug output (you might remove these prints in a production build)
  Serial.print("Registered command: ");
  Serial.print(name);
  Serial.print(" at index ");
  Serial.print(index);
  Serial.print(" with value ");
  Serial.println(commandTable[index].value);
}

// ----- File Reading Helper -----
String readFileDevices(fs::FS &fs, const char *path) {
  String outContent = "";
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return "- failed to open file for reading";
  }
  size_t fileSize = file.size();
  std::unique_ptr<char[]> buf(new char[fileSize + 1]);
  file.readBytes(buf.get(), fileSize);
  buf[fileSize] = '\0'; // Null-terminate
  outContent = String(buf.get());
  file.close();
  Serial.println(outContent);
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

  // Use a fixed-size JSON document to minimize dynamic memory allocation.
  JsonDocument device;
  DeserializationError error = deserializeJson(device, deviceFileContent);
  deviceFileContent.clear();

  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.f_str());
    return false;
  }

  JsonArray commandArray = device.as<JsonArray>();
  if (!commandArray) {
    Serial.println("JSON is not an array!");
    return false;
  }

  // Iterate over each command object in the JSON array.
  for (JsonObject command : commandArray) {
    std::string strname = deviceName;
    std::string com = command["name"];
    std::string finalName = strname + "_" + com;
    const char* name = finalName.c_str();
    const char* commandType = command["commandType"];
    const char* commandData = command["commandData"];
    const char* commandDataExtended = command["commandDataExtended"];

    Serial.print("Registering command: ");
    Serial.println(name ? name : "N/A");
    if (name && commandType && commandData && commandDataExtended) {
      register_command_dynamic(name, commandType, commandData, commandDataExtended);
    }
  }

  return true;
}

void clearCommands(){
    // Clear out the commandTable to prepare for clean data
    for (int i = 0; i < MAX_COMMANDS; ++i) {
        std::memset(commandTable[i].name, 0, MAX_NAME_LEN);  // Clear the name
        commandTable[i].value = 0;                           // Reset the value
        commandTable[i].inUse = false;                       // Set inUse to false
    }
}

// Reads a master devices file and registers each dynamic device.
void register_dynamic_devices() {
  Serial.begin(115200);
  delay(100);
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  clearCommands();
  String devicesFileContent = SPIFFS.exists("/devices.json") ? readFileDevices(SPIFFS, "/devices.json") : "[]";

  JsonDocument devices;
  DeserializationError error = deserializeJson(devices, devicesFileContent);
  devicesFileContent.clear();

  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.f_str());
    return;
  }

  JsonArray deviceArray = devices.as<JsonArray>();
  if (!deviceArray) {
    Serial.println("devices.json is not an array!");
    return;
  }

  // For each device name in devices.json, register its commands.
  for (JsonVariant value : deviceArray) {
    const char* deviceName = value.as<const char*>();
    Serial.print("Registering device: ");
    Serial.println(deviceName);
    register_dynamic_device(deviceName);
  }
}

// ----- Example: Lookup for a Command -----
// This demonstrates how you can later calculate the memory location using the same hash.
void lookup_command(const char* lookupName) {
  uint8_t index = hashIndex(lookupName);
  uint8_t originalIndex = index;
  bool found = false;
  while (commandTable[index].inUse) {
    if (strcmp(commandTable[index].name, lookupName) == 0) {
      Serial.print("Lookup for ");
      Serial.print(lookupName);
      Serial.print(" found value: ");
      Serial.println(commandTable[index].value);
      found = true;
      break;
    }
    index = (index + 1) % MAX_COMMANDS;
    if (index == originalIndex) break; // Searched entire table
  }
  if (!found) {
    Serial.print("Command ");
    Serial.print(lookupName);
    Serial.println(" not found.");
  }
}
