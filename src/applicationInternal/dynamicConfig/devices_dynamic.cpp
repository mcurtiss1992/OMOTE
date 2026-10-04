#include <cstring>     // For memset
#include <cstdio>      // For snprintf
#include <cstdlib>     // For strtol, strtoull
#include <string>      // For std::string
#include <vector>
#include <list>
#include <ArduinoJson.h>
#include "applicationInternal/commandHandler.h"
#include "applicationInternal/hardware/hardwarePresenter.h"
#include "applicationInternal/omote_log.h"
#include "devices_dynamic.h"

// Packed store for all dynamic commands. A std::map of std::list<std::string> costs roughly 150 bytes of
// bookkeeping per command and a fixed hash table another 36, while the data itself is ~40 bytes.
// Each record in cmdPool is: id (uint16), commandHandler (uint8), number of strings (uint8),
// then the command name and the payload strings, each 0 terminated.
// cmdIndex holds the offset of every record, ordered by id (ids are handed out in increasing order).
static std::vector<char> cmdPool;
static std::vector<uint16_t> cmdIndex;
#define CMD_RECORD_HEADER 4

// ----- JSON helper -----
bool loadJsonFile(const std::string& filename, JsonDocument& doc) {
  std::string content;
  if (!readConfigFile(filename, content)) {
    omote_log_d("Config file not found: %s\r\n", filename.c_str());
    return false;
  }
  DeserializationError error = deserializeJson(doc, content);
  if (error) {
    omote_log_e("Failed to parse %s: %s\r\n", filename.c_str(), error.c_str());
    return false;
  }
  return true;
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

  // lookups build the name with snprintf(MAX_NAME_LEN), so store it truncated the same way
  std::string storedName(name);
  if (storedName.size() > MAX_NAME_LEN - 1) storedName.resize(MAX_NAME_LEN - 1);

  size_t recordSize = CMD_RECORD_HEADER + storedName.size() + 1;
  for (const std::string& payload : cmd.commandPayloads) recordSize += payload.size() + 1;
  if (cmdIndex.size() >= MAX_COMMANDS) {
    omote_log_e("Error: Command table full!\r\n");
    return;
  }
  if (cmdPool.size() + recordSize > 0xFFFF || cmd.commandPayloads.size() > 255) {
    omote_log_e("Error: Command storage full!\r\n");
    return;
  }

  uint16_t cmdId = 0;
  get_uniqueCommandID(&cmdId);

  uint16_t offset = cmdPool.size();
  cmdPool.push_back(cmdId & 0xFF);
  cmdPool.push_back(cmdId >> 8);
  cmdPool.push_back((char)cmd.commandHandler);
  cmdPool.push_back((char)cmd.commandPayloads.size());
  cmdPool.insert(cmdPool.end(), storedName.c_str(), storedName.c_str() + storedName.size() + 1);
  for (const std::string& payload : cmd.commandPayloads) {
    cmdPool.insert(cmdPool.end(), payload.c_str(), payload.c_str() + payload.size() + 1);
  }
  cmdIndex.push_back(offset);

  omote_log_d("Registered command: %s with value %d\r\n", name, cmdId);
}

static uint16_t recordId(uint16_t offset) {
  return (uint8_t)cmdPool[offset] | ((uint8_t)cmdPool[offset + 1] << 8);
}

bool getDynamicCommandData(uint16_t id, commandData& out) {
  size_t lo = 0, hi = cmdIndex.size();
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;
    uint16_t midId = recordId(cmdIndex[mid]);
    if (midId == id) {
      const char* p = &cmdPool[cmdIndex[mid]];
      out.commandHandler = (commandHandlers)(uint8_t)p[2];
      uint8_t count = (uint8_t)p[3];
      p += CMD_RECORD_HEADER;
      p += strlen(p) + 1; // skip the name
      out.commandPayloads.clear();
      for (uint8_t i = 0; i < count; i++) {
        out.commandPayloads.push_back(p);
        p += strlen(p) + 1;
      }
      return true;
    }
    if (midId < id) lo = mid + 1; else hi = mid;
  }
  return false;
}

// ----- Dynamic Device Registration -----
// Reads a device's JSON file and registers each command dynamically.
bool register_dynamic_device(const char *deviceName) {
  std::string deviceFilePath = std::string("device_") + deviceName + ".json";

  JsonDocument device;
  if (!loadJsonFile(deviceFilePath, device)) {
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
  cmdPool.clear();
  cmdPool.shrink_to_fit();
  cmdIndex.clear();
  cmdIndex.shrink_to_fit();
}

// Reads a master devices file and registers each dynamic device.
void register_dynamic_devices() {
  clearCommands();

  JsonDocument devices;
  if (!loadJsonFile("devices.json", devices)) {
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

  // the vectors grew by doubling while registering, give back the unused part
  cmdPool.shrink_to_fit();
  cmdIndex.shrink_to_fit();
}

// ----- Command lookup -----
// Looks up the command value by constructing the command name from the device and command strings.
// Lookups only happen when GUIs and scenes are loaded, so a linear search is fine. A command that was
// registered twice under the same name resolves to the later one.
uint16_t getCommandValue(const char* device, const char* command) {
  char commandName[MAX_NAME_LEN];
  snprintf(commandName, MAX_NAME_LEN, "%s_%s", device, command);
  for (size_t i = cmdIndex.size(); i > 0; i--) {
    uint16_t offset = cmdIndex[i - 1];
    if (strcmp(&cmdPool[offset + CMD_RECORD_HEADER], commandName) == 0) {
      return recordId(offset);
    }
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

bool executeDynamicCommand(std::string device, std::string command, std::string payload) {
  uint16_t commandId = resolveDynamicCommand(device.c_str(), command.c_str());
  if (commandId == DYNAMIC_COMMAND_NOT_FOUND) {
    return false;
  }
  executeCommand(commandId, payload);
  return true;
}
