#ifndef DYNAMIC_SCENE_REGISTRATION_H
#define DYNAMIC_SCENE_REGISTRATION_H

#include "devices_dynamic.h"

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
 * @brief Reads the master scenes file ("/scenes.json") and registers each dynamic scene.
 *
 * For every scene, "/scene_<name>.json" holds the key mapping and "/<name>_gui_extras.json"
 * the on/off sequences and the GUIs shown while the scene is active.
 * Has to be called after the dynamic devices and GUIs have been registered.
 */
void register_dynamic_scenes();

#endif // DYNAMIC_SCENE_REGISTRATION_H
