#ifndef DYNAMIC_SCENE_REGISTRATION_H
#define DYNAMIC_SCENE_REGISTRATION_H

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <map>
#include <cstring>
#include "devices_dynamic.h"
// Include your scene registry and command handler headers for key codes
// and command registration functions.
#include "applicationInternal/scenes/sceneRegistry.h"   // Provides definitions for KEY_UP, KEY_VOLUP, etc.
#include "applicationInternal/commandHandler.h"         // Provides register_command() and makeCommandData()

// --- Function Prototypes ---

/**
 * @brief Reads the content of a file from SPIFFS.
 *
 * @param path The file path.
 * @return A String containing the file content.
 */
String readFileContent(const char* path);


uint16_t getBLECommandValue(const std::string& commandName);

/**
 * @brief Maps a button name (from the scene JSON) to its corresponding key code.
 *
 * Button names correspond to those used in the remote's HTML (e.g., "Up", "VolumeUp", "Red", etc.).
 *
 * @param keyName The name of the key.
 * @return The key code defined in your sceneRegistry (or 0 if not found).
 */
char getKeyCode(const char* keyName);

/**
 * @brief Looks up the registered command value based on the device and command strings.
 *
 * This function constructs a command name (e.g., "TV_test") and searches the command table.
 *
 * @param device The device name.
 * @param command The command name.
 * @return The command value if found; otherwise, 0.
 */
uint16_t getCommandValue(const char* device, const char* command);

/**
 * @brief Registers a dynamic scene by reading its JSON file from SPIFFS.
 *
 * The scene JSON file is expected to be named in the format "/scene_<sceneName>.json".
 * This function parses the JSON, maps button names to command values, and then registers
 * the scene with its dynamic key mapping.
 *
 * @param sceneName The name of the scene.
 */
void register_dynamic_scene(const char* sceneName);

/**
 * @brief Reads the master scenes file ("/scenes.json") and registers each dynamic scene.
 */
void register_dynamic_scenes();

#endif // DYNAMIC_SCENE_REGISTRATION_H
