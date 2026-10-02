#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <map>
#include <cstring>
#include "applicationInternal/scenes/sceneRegistry.h"    // Provides key codes like KEY_UP, KEY_VOLUP, etc.
#include "applicationInternal/commandHandler.h"          // Provides register_command() and makeCommandData()
#include "applicationInternal/gui/guiBase.h"
#include "applicationInternal/scenes/sceneHandler.h"
#include "applicationInternal/omote_log.h"
#include "devices_dynamic.h"

// --- Key Mapping Function ---
char getKeyCode(const char* keyName) {
    if (strcmp_P(keyName, PSTR("Power")) == 0)        return KEY_OFF;
    else if (strcmp_P(keyName, PSTR("Stop")) == 0)      return KEY_STOP;
    else if (strcmp_P(keyName, PSTR("Rewind")) == 0)    return KEY_REWI;
    else if (strcmp_P(keyName, PSTR("PlayPause")) == 0) return KEY_PLAY;
    else if (strcmp_P(keyName, PSTR("Forward")) == 0)   return KEY_FORW;
    else if (strcmp_P(keyName, PSTR("Guide")) == 0)     return KEY_CONF; // adjust if needed
    else if (strcmp_P(keyName, PSTR("Info")) == 0)      return KEY_INFO;
    else if (strcmp_P(keyName, PSTR("Up")) == 0)        return KEY_UP;
    else if (strcmp_P(keyName, PSTR("Down")) == 0)      return KEY_DOWN;
    else if (strcmp_P(keyName, PSTR("Left")) == 0)      return KEY_LEFT;
    else if (strcmp_P(keyName, PSTR("Right")) == 0)     return KEY_RIGHT;
    else if (strcmp_P(keyName, PSTR("OK")) == 0)        return KEY_OK;
    else if (strcmp_P(keyName, PSTR("Back")) == 0)      return KEY_BACK;
    else if (strcmp_P(keyName, PSTR("Source")) == 0)    return KEY_SRC;
    else if (strcmp_P(keyName, PSTR("VolumeUp")) == 0)  return KEY_VOLUP;
    else if (strcmp_P(keyName, PSTR("VolumeDown")) == 0)return KEY_VOLDO;
    else if (strcmp_P(keyName, PSTR("Mute")) == 0)      return KEY_MUTE;
    else if (strcmp_P(keyName, PSTR("Record")) == 0)    return KEY_REC;
    else if (strcmp_P(keyName, PSTR("ChannelUp")) == 0) return KEY_CHUP;
    else if (strcmp_P(keyName, PSTR("ChannelDown")) == 0)return KEY_CHDOW;
    else if (strcmp_P(keyName, PSTR("Red")) == 0)       return KEY_RED;
    else if (strcmp_P(keyName, PSTR("Green")) == 0)     return KEY_GREEN;
    else if (strcmp_P(keyName, PSTR("Yellow")) == 0)    return KEY_YELLO;
    else if (strcmp_P(keyName, PSTR("Blue")) == 0)      return KEY_BLUE;
    return 0; // Unknown key
}

uint16_t getBLECommandValue(const std::string& commandName) {
    if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_UP")) == 0) return KEYBOARD_BLE_UP;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_DOWN")) == 0) return KEYBOARD_BLE_DOWN;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_RIGHT")) == 0) return KEYBOARD_BLE_RIGHT;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_LEFT")) == 0) return KEYBOARD_BLE_LEFT;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_SELECT")) == 0) return KEYBOARD_BLE_SELECT;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_SENDSTRING")) == 0) return KEYBOARD_BLE_SENDSTRING;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_BACK")) == 0) return KEYBOARD_BLE_BACK;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_HOME")) == 0) return KEYBOARD_BLE_HOME;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_MENU")) == 0) return KEYBOARD_BLE_MENU;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_SCAN_PREVIOUS_TRACK")) == 0) return KEYBOARD_BLE_SCAN_PREVIOUS_TRACK;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_REWIND_LONG")) == 0) return KEYBOARD_BLE_REWIND_LONG;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_REWIND")) == 0) return KEYBOARD_BLE_REWIND;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_PLAYPAUSE")) == 0) return KEYBOARD_BLE_PLAYPAUSE;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_FASTFORWARD")) == 0) return KEYBOARD_BLE_FASTFORWARD;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_FASTFORWARD_LONG")) == 0) return KEYBOARD_BLE_FASTFORWARD_LONG;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_SCAN_NEXT_TRACK")) == 0) return KEYBOARD_BLE_SCAN_NEXT_TRACK;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_MUTE")) == 0) return KEYBOARD_BLE_MUTE;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_VOLUME_INCREMENT")) == 0) return KEYBOARD_BLE_VOLUME_INCREMENT;
    else if (strcmp_P(commandName.c_str(), PSTR("KEYBOARD_BLE_VOLUME_DECREMENT")) == 0) return KEYBOARD_BLE_VOLUME_DECREMENT;
    else {
        throw std::invalid_argument("Unknown command: " + commandName);
    }
}

// --- File Reading Helper ---
// Reads file content from SPIFFS.
String readFileContent(const char* path) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    omote_log_e("Failed to open file: %s\n", path);
    return "";
  }
  String content = file.readString();
  file.close();
  return content;
}

// --- Command Lookup Helper ---
// Looks up the command value (registered in your system) by constructing the command name from the device and command strings.
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
  return 0;  // Not found
}

// --- Dynamic Scene Registration ---
// Registers one dynamic scene by reading its JSON file and mapping short key commands.
void register_dynamic_scene(const char* sceneName) {
  // Construct the file path for the scene JSON (e.g., "/scene_newScene2.json")
  String filePath = String(F("/scene_")) + sceneName + F(".json");
  
  String sceneContent = readFileContent(filePath.c_str());
  if (sceneContent == "") {
    omote_log_e("Scene file not found or empty: %s\n", filePath.c_str());
    return;
  }

  // Use a JsonDocument to parse the scene JSON.
  JsonDocument sceneDoc;
  DeserializationError error = deserializeJson(sceneDoc, sceneContent);
  if (error) {
    omote_log_e("Failed to parse scene JSON for '%s': %s\n", sceneName, error.f_str());
    return;
  }

  // Create new maps for short key commands, long key commands, and repeat modes.
  std::map<char, uint16_t>* key_commands_short = new std::map<char, uint16_t>();
  std::map<char, uint16_t>* key_commands_long = new std::map<char, uint16_t>();
  std::map<char, repeatModes>* key_repeatModes_dynamic = new std::map<char, repeatModes>();

  JsonObject sceneObj = sceneDoc.as<JsonObject>();
  // Iterate over each key in the JSON object.
  for (JsonPair kv : sceneObj) {
    const char* key = kv.key().c_str();
    // Skip reserved keys (e.g., "sequence")
    if (strcmp_P(key, PSTR("sequence")) == 0) continue;

    JsonObject cmdObj = kv.value().as<JsonObject>();
    if (!cmdObj["device"].is<const char*>() || !cmdObj["command"].is<const char*>()) {
        omote_log_e("Invalid command format for key: %s\n", key);
        continue;
    }
    const char* device = cmdObj["device"];
    const char* command = cmdObj["command"];

    // Look up the command value using the combined "device_command" naming scheme.
    uint16_t cmdVal = getCommandValue(device, command);
    char keyIndex = getKeyCode(key);
    if (strcmp_P(device, PSTR("BLE")) == 0) {
      (*key_commands_short)[keyIndex] = getBLECommandValue(command);
      omote_log_i("Scene '%s': Mapped key '%s' (normalized index '%c') to BLE command '%s' (value %u)\n",
        sceneName, key, keyIndex, command, getBLECommandValue(command));
      continue;
    }
    if (cmdVal == 0) {
      omote_log_e("Command not found: %s_%s\n", device, command);
      continue;
    }
    
    (*key_commands_short)[keyIndex] = cmdVal;
    omote_log_i("Scene '%s': Mapped key '%s' (normalized index '%c') to command '%s_%s' (value %u)\n",
                  sceneName, key, keyIndex, device, command, cmdVal);
  }

  // Create dummy functions for setting keys, starting, and ending the scene.
  auto setKeysFunc = [](){ /* Keys are set via the dynamic mapping; no additional setup needed */ };
  auto startSequence = [](){ /* Add scene start sequence logic here if desired */ };
  auto endSequence = [](){ /* Add scene end sequence logic here if needed */ };

  // Dummy GUI list – adjust or extend this as needed for your UI.
  static t_gui_list dynamic_gui_list = {};

  // Assign a unique command id for the scene.
  uint16_t sceneCommandId;
  uint16_t sceneCommandIdForce;
  // Register the scene as a command.
  register_command(&sceneCommandId, makeCommandData(SCENE, {sceneName}));
  register_command(&sceneCommandIdForce, makeCommandData(SCENE, {sceneName, "FORCE"}));

  // Finally, register the scene with your system.
  register_scene(
    sceneName,                 // Scene name
    setKeysFunc,               // Function to set keys
    startSequence,             // Scene start sequence function
    endSequence,               // Scene end sequence function
    key_repeatModes_dynamic,   // key_repeatModes (not used for short key presses)
    key_commands_short,        // Pointer to short key commands mapping
    key_commands_long,         // Pointer to long key commands mapping (not used here)
    NULL,                      // GUI list for the scene
    sceneCommandId             // Unique scene command id
  );
  sceneObj.clear();
  sceneDoc.clear();
  sceneContent.clear();
}

// Reads the master scenes file ("/scenes.json") and registers each dynamic scene.
void register_dynamic_scenes() {
  const char* scenesFilePath = "/scenes.json";
  String scenesContent = readFileContent(scenesFilePath);
  if (scenesContent == "") {
    omote_log_e("Scenes file not found or empty: %s\n", scenesFilePath);
    return;
  }

  // Use a JsonDocument to parse the master scenes file.
  JsonDocument scenesDoc;
  DeserializationError error = deserializeJson(scenesDoc, scenesContent);
  if (error) {
    omote_log_e("Failed to parse scenes JSON: %s\n", error.f_str());
    return;
  }

  JsonObject scenesObj = scenesDoc.as<JsonObject>();
  JsonArray scenesArray = scenesObj["scenes"].as<JsonArray>();
  if (!scenesArray) {
    omote_log_e("Invalid scenes.json format: 'scenes' array not found.");
    return;
  }

  // Loop through each scene name in the JSON array and register it.
  for (JsonVariant sceneNameVariant : scenesArray) {
    const char* sceneName = sceneNameVariant.as<const char*>();
    if (sceneName) {
      omote_log_i("Registering dynamic scene: %s\n", sceneName);
      register_dynamic_scene(sceneName);
    }
  }
  scenesObj.clear();
  scenesArray.clear();
}
