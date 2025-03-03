#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <map>
#include <cstring>
#include "applicationInternal/scenes/sceneRegistry.h"    // Provides key codes like KEY_UP, KEY_VOLUP, etc.
#include "applicationInternal/commandHandler.h"          // Provides register_command() and makeCommandData()
#include "devices_dynamic.h"

// --- Assumed External Definitions ---
// These symbols are assumed to be defined elsewhere in your code base.
extern uint16_t nextCommandValue;    // Global command counter
extern CommandEntry commandTable[MAX_COMMANDS];
extern uint8_t hashIndex(const char* str);

// --- Key Mapping Function ---
// Maps a button name (from the scene JSON file) to its key code, based on your remote's HTML.
char getKeyCode(const char* keyName) {
  if (strcmp(keyName, "Power") == 0)       return KEY_OFF;
  else if (strcmp(keyName, "Stop") == 0)     return KEY_STOP;
  else if (strcmp(keyName, "Rewind") == 0)   return KEY_REWI;
  else if (strcmp(keyName, "PlayPause") == 0)return KEY_PLAY;
  else if (strcmp(keyName, "Forward") == 0)  return KEY_FORW;
  else if (strcmp(keyName, "Guide") == 0)    return KEY_CONF; // Assumed mapping; adjust if needed.
  else if (strcmp(keyName, "Back") == 0)     return KEY_BACK;
  else if (strcmp(keyName, "Up") == 0)       return KEY_UP;
  else if (strcmp(keyName, "Down") == 0)     return KEY_DOWN;
  else if (strcmp(keyName, "Left") == 0)     return KEY_LEFT;
  else if (strcmp(keyName, "Right") == 0)    return KEY_RIGHT;
  else if (strcmp(keyName, "OK") == 0)       return KEY_OK;
  else if (strcmp(keyName, "Info") == 0)     return KEY_INFO;
  else if (strcmp(keyName, "Exit") == 0)     return KEY_SRC;  // No dedicated Exit key; using KEY_SRC.
  else if (strcmp(keyName, "VolumeUp") == 0) return KEY_VOLUP;
  else if (strcmp(keyName, "VolumeDown") == 0)return KEY_VOLDO;
  else if (strcmp(keyName, "Mute") == 0)     return KEY_MUTE;
  else if (strcmp(keyName, "Record") == 0)   return KEY_REC;
  else if (strcmp(keyName, "ChannelUp") == 0)return KEY_CHUP;
  else if (strcmp(keyName, "ChannelDown") == 0)return KEY_CHDOW;
  else if (strcmp(keyName, "Red") == 0)      return KEY_RED;
  else if (strcmp(keyName, "Green") == 0)    return KEY_GREEN;
  else if (strcmp(keyName, "Yellow") == 0)   return KEY_YELLO;
  else if (strcmp(keyName, "Blue") == 0)     return KEY_BLUE;
  return 0;  // Unknown key
}

// --- File Reading Helper ---
// Reads file content from SPIFFS.
String readFileContent(const char* path) {
  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.printf("Failed to open file: %s\n", path);
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
  String filePath = "/scene_";
  filePath += sceneName;
  filePath += ".json";
  
  String sceneContent = readFileContent(filePath.c_str());
  if (sceneContent == "") {
    Serial.printf("Scene file not found or empty: %s\n", filePath.c_str());
    return;
  }

  // Use a fixed-size JSON document to minimize dynamic memory allocation.
  JsonDocument sceneDoc;
  DeserializationError error = deserializeJson(sceneDoc, sceneContent);
  if (error) {
    Serial.printf("Failed to parse scene JSON for '%s': %s\n", sceneName, error.f_str());
    return;
  }

  // Create a new map for short key commands for this scene.
  std::map<char, uint16_t>* key_commands_short = new std::map<char, uint16_t>();
  std::map<char, uint16_t>* key_commands_long;
  std::map<char, repeatModes>* key_repeatModes_dynamic;


  JsonObject sceneObj = sceneDoc.as<JsonObject>();
  // Iterate over each key in the JSON object.
  for (JsonPair kv : sceneObj) {
    const char* key = kv.key().c_str();
    // Skip reserved keys (e.g., "sequence")
    if (strcmp(key, "sequence") == 0) continue;

    JsonObject cmdObj = kv.value().as<JsonObject>();
    if (!cmdObj["device"].is<const char*>() || !cmdObj["command"].is<const char*>()) {
        Serial.printf("Invalid command format for key: %s\n", key);
        continue;
    }
    const char* device = cmdObj["device"];
    const char* command = cmdObj["command"];

    // Look up the command value using the combined "device_command" naming scheme.
    uint16_t cmdVal = getCommandValue(device, command);
    if (cmdVal == 0) {
      Serial.printf("Command not found: %s_%s\n", device, command);
      continue;
    }
    char keyCode = getKeyCode(key);
    if (keyCode == 0) {
      Serial.printf("Key code not found for key: %s\n", key);
      continue;
    }
    // Map the key code to the command value.
    (*key_commands_short)[keyCode] = cmdVal;
    Serial.printf("Scene '%s': Mapped key '%s' (code '%c') to command '%s_%s' (value %u)\n",
                  sceneName, key, keyCode, device, command, cmdVal);
  }
  key_commands_long = {
  
  
  };

  key_repeatModes_dynamic = {
  
  
  
  
  
  
  
  
  
  
  };

  // Create dummy functions for setting keys, starting, and ending the scene.
  auto setKeysFunc = [](){ /* Keys are set via the dynamic mapping; no additional setup needed */ };
  auto startSequence = [](){ /* Add scene start sequence logic here if desired */ };
  auto endSequence = [](){ /* Add scene end sequence logic here if needed */ };

  // Dummy GUI list – adjust or extend this as needed for your UI.
  static t_gui_list dynamic_gui_list = {};

  // Assign a unique command id for the scene.
  uint16_t sceneCommandId = nextCommandValue++;
  // Register the scene as a command. The makeCommandData() function and SCENE identifier
  // must be defined in your system.
  register_command(&sceneCommandId, makeCommandData(SCENE, {sceneName}));

  // Finally, register the scene with your system.
  register_scene(
    sceneName,           // Scene name
    setKeysFunc,         // Function to set keys (already handled dynamically)
    startSequence,       // Scene start sequence function
    endSequence,         // Scene end sequence function
    key_repeatModes_dynamic,             // key_repeatModes (not used for short key presses)
    key_commands_short,  // Pointer to short key commands mapping
    key_commands_long,             // long key commands mapping (not used here)
    NULL,   // GUI list for the scene
    sceneCommandId       // Unique scene command id
  );
}

// Reads the master scenes file ("/scenes.json") and registers each dynamic scene.
void register_dynamic_scenes() {
  const char* scenesFilePath = "/scenes.json";
  String scenesContent = readFileContent(scenesFilePath);
  if (scenesContent == "") {
    Serial.printf("Scenes file not found or empty: %s\n", scenesFilePath);
    return;
  }

  // Use a fixed-size JSON document for the master scenes file.
  JsonDocument scenesDoc;
  DeserializationError error = deserializeJson(scenesDoc, scenesContent);
  if (error) {
    Serial.printf("Failed to parse scenes JSON: %s\n", error.f_str());
    return;
  }

  JsonObject scenesObj = scenesDoc.as<JsonObject>();
  JsonArray scenesArray = scenesObj["scenes"].as<JsonArray>();
  if (!scenesArray) {
    Serial.println("Invalid scenes.json format: 'scenes' array not found.");
    return;
  }

  // Loop through each scene name in the JSON array and register it.
  for (JsonVariant sceneNameVariant : scenesArray) {
    const char* sceneName = sceneNameVariant.as<const char*>();
    if (sceneName) {
      Serial.printf("Registering dynamic scene: %s\n", sceneName);
      register_dynamic_scene(sceneName);
    }
  }
}
