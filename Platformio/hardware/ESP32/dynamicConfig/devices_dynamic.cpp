#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <cstring>     // For memset
#include <cstdlib>     // For strtol, strtoull
#include <memory>      // For std::unique_ptr
#include <new>         // For std::nothrow
#include <string>      // For std::string
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/omote_log.h"
#include "devices_dynamic.h"

// Preallocated table for command entries
CommandEntry commandTable[MAX_COMMANDS];

// ----- SPIFFS / JSON helpers -----
bool mountConfigFS() {
  static bool mounted = false;
  if (!mounted) {
    mounted = SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED);
    if (!mounted) {
      omote_log_e("SPIFFS Mount Failed\r\n");
    }
  }
  return mounted;
}

bool loadJsonFile(const char* path, JsonDocument& doc) {
  if (!mountConfigFS() || !SPIFFS.exists(path)) {
    omote_log_d("Config file not found: %s\r\n", path);
    return false;
  }
  File file = SPIFFS.open(path, FILE_READ);
  if (!file || file.isDirectory()) {
    omote_log_e("Failed to open file for reading: %s\r\n", path);
    return false;
  }
  size_t fileSize = file.size();
  std::unique_ptr<char[]> buf(new (std::nothrow) char[fileSize + 1]);
  if (!buf) {
    omote_log_e("Not enough memory to read %s (%u bytes)\r\n", path, fileSize);
    return false;
  }
  size_t bytesRead = file.read((uint8_t*)buf.get(), fileSize);
  file.close();

  DeserializationError error = deserializeJson(doc, (const char*)buf.get(), bytesRead);
  if (error) {
    omote_log_e("Failed to parse %s: %s\r\n", path, error.c_str());
    return false;
  }
  return true;
}

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

static bool isInteger(const char* s) {
  if (s == NULL || *s == '\0') return false;
  char* end;
  strtol(s, &end, 10);
  return *end == '\0';
}

static bool isUnsignedNumber(const char* s) {
  if (s == NULL || *s == '\0') return false;
  char* end;
  strtoull(s, &end, 0); // base 0 accepts decimal and 0x... hex
  return *end == '\0';
}

// Register a command into the fixed table using linear probing.
void register_command_dynamic(const char* name, const char* commandType, const char* data, const char* dataExtended) {
  // Build the command first, so that invalid entries never end up in the table.
  // Invalid numbers would otherwise throw inside std::stoi/std::stoull when the key is pressed.
  commandData cmd;
  if (strcmp(commandType, "IR") == 0) {
    // data: IR protocol number, dataExtended: code (for protocol 0/GC a comma separated list)
    if (!isInteger(data)) {
      omote_log_e("Command %s: IR protocol '%s' is not a number, command ignored\r\n", name, data);
      return;
    }
    if ((atoi(data) != 0) && !isUnsignedNumber(dataExtended)) {
      omote_log_e("Command %s: IR code '%s' is not a number, command ignored\r\n", name, dataExtended);
      return;
    }
    // Both payloads have to be strings. Passing the protocol as int made the braced list pick the
    // list(count, value) constructor, so the protocol number was lost and IR commands never worked.
    cmd = makeCommandData(IR, {std::string(data), std::string(dataExtended)});
#if (ENABLE_WIFI_AND_MQTT == 1)
  } else if (strcmp(commandType, "MQTT") == 0) {
    // data: topic, dataExtended: payload
    cmd = makeCommandData(MQTT, {data, dataExtended});
#endif
#if (ENABLE_KEYBOARD_BLE == 1)
  } else if (strcmp(commandType, "BLE") == 0) {
    // data: peer address, dataExtended: key, either a KEYBOARD_BLE_* name or a command id
    std::string key = dataExtended;
    if (!isInteger(dataExtended)) {
      uint16_t keyCommand = getBLECommandValue(key);
      if (keyCommand == DYNAMIC_COMMAND_NOT_FOUND) {
        omote_log_e("Command %s: unknown BLE key '%s', command ignored\r\n", name, dataExtended);
        return;
      }
      key = std::to_string(keyCommand);
    }
    cmd = makeCommandData(BLE_KEYBOARD, {data, key});
#endif
  } else {
    omote_log_e("Command %s: unknown command type '%s', command ignored\r\n", name, commandType);
    return;
  }

  uint8_t index = hashIndex(name);
  uint8_t originalIndex = index;

  // Linear probing to resolve collisions.
  while (commandTable[index].inUse && strncmp(commandTable[index].name, name, MAX_NAME_LEN - 1) != 0) {
    index = (index + 1) % MAX_COMMANDS;
    if (index == originalIndex) {
      omote_log_e("Error: Command table full!\r\n");
      return;
    }
  }

  // Save the command name into the table.
  strncpy(commandTable[index].name, name, MAX_NAME_LEN);
  commandTable[index].name[MAX_NAME_LEN - 1] = '\0'; // Ensure null termination

  uint16_t cmdId = 0;
  register_command(&cmdId, cmd);
  commandTable[index].value = cmdId;
  commandTable[index].inUse = true;

  omote_log_d("Registered command: %s at index %d with value %d\r\n", name, index, cmdId);
}

// ----- Dynamic Device Registration -----
// Reads a device's JSON file and registers each command dynamically.
boolean register_dynamic_device(const char *deviceName) {
  String deviceFilePath = String("/device_") + deviceName + ".json";

  JsonDocument device;
  if (!loadJsonFile(deviceFilePath.c_str(), device)) {
    return false;
  }

  JsonArray commandArray = device.as<JsonArray>();
  if (!commandArray) {
    omote_log_e("%s is not an array!\r\n", deviceFilePath.c_str());
    return false;
  }

  // Iterate over each command object in the JSON array.
  for (JsonObject command : commandArray) {
    const char* commandName = command["name"];
    const char* commandType = command["commandType"];
    const char* data = command["commandData"] | "";
    const char* dataExtended = command["commandDataExtended"] | "";
    if (!commandName || !commandType || commandName[0] == '\0') {
      omote_log_w("Device %s: skipping command without name or type\r\n", deviceName);
      continue;
    }
    std::string finalName = std::string(deviceName) + "_" + commandName;
    register_command_dynamic(finalName.c_str(), commandType, data, dataExtended);
  }
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
  clearCommands();

  JsonDocument devices;
  if (!loadJsonFile("/devices.json", devices)) {
    return;
  }

  JsonArray deviceArray = devices.as<JsonArray>();
  if (!deviceArray) {
    omote_log_e("devices.json is not an array!\r\n");
    return;
  }

  // For each device name in devices.json, register its commands.
  for (JsonVariant value : deviceArray) {
    const char* deviceName = value.as<const char*>();
    if (deviceName == NULL) continue;
    omote_log_i("Registering device: %s\r\n", deviceName);
    register_dynamic_device(deviceName);
  }
}

// ----- Command lookup -----
// Looks up the command value by constructing the command name from the device and command strings.
uint16_t getCommandValue(const char* device, const char* command) {
  char commandName[MAX_NAME_LEN];
  snprintf(commandName, MAX_NAME_LEN, "%s_%s", device, command);
  uint8_t index = hashIndex(commandName);
  uint8_t originalIndex = index;
  while (commandTable[index].inUse) {
    if (strcmp(commandTable[index].name, commandName) == 0) {
      return commandTable[index].value;
    }
    index = (index + 1) % MAX_COMMANDS;
    if (index == originalIndex) break;
  }
  return DYNAMIC_COMMAND_NOT_FOUND;
}

uint16_t getBLECommandValue(const std::string& commandName) {
#if (ENABLE_KEYBOARD_BLE == 1)
  static const struct { const char* name; uint16_t* command; } bleKeys[] = {
    {"KEYBOARD_BLE_UP",                  &KEYBOARD_BLE_UP},
    {"KEYBOARD_BLE_DOWN",                &KEYBOARD_BLE_DOWN},
    {"KEYBOARD_BLE_RIGHT",               &KEYBOARD_BLE_RIGHT},
    {"KEYBOARD_BLE_LEFT",                &KEYBOARD_BLE_LEFT},
    {"KEYBOARD_BLE_SELECT",              &KEYBOARD_BLE_SELECT},
    {"KEYBOARD_BLE_SENDSTRING",          &KEYBOARD_BLE_SENDSTRING},
    {"KEYBOARD_BLE_BACK",                &KEYBOARD_BLE_BACK},
    {"KEYBOARD_BLE_HOME",                &KEYBOARD_BLE_HOME},
    {"KEYBOARD_BLE_MENU",                &KEYBOARD_BLE_MENU},
    {"KEYBOARD_BLE_SCAN_PREVIOUS_TRACK", &KEYBOARD_BLE_SCAN_PREVIOUS_TRACK},
    {"KEYBOARD_BLE_REWIND_LONG",         &KEYBOARD_BLE_REWIND_LONG},
    {"KEYBOARD_BLE_REWIND",              &KEYBOARD_BLE_REWIND},
    {"KEYBOARD_BLE_PLAYPAUSE",           &KEYBOARD_BLE_PLAYPAUSE},
    {"KEYBOARD_BLE_FASTFORWARD",         &KEYBOARD_BLE_FASTFORWARD},
    {"KEYBOARD_BLE_FASTFORWARD_LONG",    &KEYBOARD_BLE_FASTFORWARD_LONG},
    {"KEYBOARD_BLE_SCAN_NEXT_TRACK",     &KEYBOARD_BLE_SCAN_NEXT_TRACK},
    {"KEYBOARD_BLE_MUTE",                &KEYBOARD_BLE_MUTE},
    {"KEYBOARD_BLE_VOLUME_INCREMENT",    &KEYBOARD_BLE_VOLUME_INCREMENT},
    {"KEYBOARD_BLE_VOLUME_DECREMENT",    &KEYBOARD_BLE_VOLUME_DECREMENT},
  };
  for (const auto& key : bleKeys) {
    if (commandName == key.name) {
      return *key.command;
    }
  }
#endif
  return DYNAMIC_COMMAND_NOT_FOUND;
}

uint16_t resolveDynamicCommand(const char* device, const char* command) {
  if ((device == NULL) || (command == NULL) || (device[0] == '\0') || (command[0] == '\0')) {
    return DYNAMIC_COMMAND_NOT_FOUND;
  }
  if (strcmp(device, "BLE") == 0) {
    return getBLECommandValue(command);
  }
  return getCommandValue(device, command);
}
