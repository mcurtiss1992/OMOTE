#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include "applicationInternal/scenes/sceneRegistry.h"    // Provides key codes like KEY_UP, KEY_VOLUP, etc.
#include "applicationInternal/gui/guiRegistry.h"
#include "applicationInternal/commandHandler.h"          // Provides register_command() and makeCommandData()
#include "applicationInternal/omote_log.h"
#include "ESP32/sleep_hal_esp32.h"
#include "devices_dynamic.h"
#include "guis_dynamic.h"
#include "scenes_dynamic.h"

// Longest "sleep" step accepted in a scene sequence. The sequence blocks the remote while it runs.
#define MAX_SEQUENCE_SLEEP_MS 10000

struct SequenceStep {
  bool isSleep;
  uint16_t command;
  uint16_t durationMs;
};

// Everything a dynamic scene needs at runtime. Lives in a std::map, so the pointers
// handed to register_scene() stay valid.
struct DynamicScene {
  std::map<char, repeatModes> keyRepeatModes;
  std::map<char, uint16_t> keyCommandsShort;
  std::map<char, uint16_t> keyCommandsLong;
  t_gui_list guiList;
  std::vector<SequenceStep> onSequence;
  std::vector<SequenceStep> offSequence;
  uint16_t activateCommand;
  uint16_t activateCommandForce;
};
static std::map<std::string, DynamicScene> dynamicScenes;

// --- Key Mapping Function ---
// Button names as written by the config app
static const struct { const char* name; char* key; } keyNames[] = {
  {"Power",       &KEY_OFF},
  {"Stop",        &KEY_STOP},
  {"Rewind",      &KEY_REWI},
  {"PlayPause",   &KEY_PLAY},
  {"Forward",     &KEY_FORW},
  {"Guide",       &KEY_CONF},
  {"Info",        &KEY_INFO},
  {"Up",          &KEY_UP},
  {"Down",        &KEY_DOWN},
  {"Left",        &KEY_LEFT},
  {"Right",       &KEY_RIGHT},
  {"OK",          &KEY_OK},
  {"Back",        &KEY_BACK},
  {"Source",      &KEY_SRC},
  {"Exit",        &KEY_SRC}, // older versions of the config app called the source key "Exit"
  {"VolumeUp",    &KEY_VOLUP},
  {"VolumeDown",  &KEY_VOLDO},
  {"Mute",        &KEY_MUTE},
  {"Record",      &KEY_REC},
  {"ChannelUp",   &KEY_CHUP},
  {"ChannelDown", &KEY_CHDOW},
  {"Red",         &KEY_RED},
  {"Green",       &KEY_GREEN},
  {"Yellow",      &KEY_YELLO},
  {"Blue",        &KEY_BLUE},
};

char getKeyCode(const char* keyName) {
  for (const auto& k : keyNames) {
    if (strcmp(keyName, k.name) == 0) {
      return *k.key;
    }
  }
  return 0; // Unknown key
}

// --- Sequences ---
static void parseSequence(JsonArrayConst steps, std::vector<SequenceStep>& out, const char* sceneName) {
  for (JsonObjectConst step : steps) {
    const char* type = step["type"] | "command";
    if (strcmp(type, "sleep") == 0) {
      long duration = step["duration"] | 0;
      if (duration <= 0) continue;
      if (duration > MAX_SEQUENCE_SLEEP_MS) {
        omote_log_w("Scene '%s': sleep of %ld ms shortened to %d ms\r\n", sceneName, duration, MAX_SEQUENCE_SLEEP_MS);
        duration = MAX_SEQUENCE_SLEEP_MS;
      }
      out.push_back({true, 0, (uint16_t)duration});
    } else {
      const char* device = step["device"] | "";
      const char* command = step["command"] | "";
      uint16_t cmd = resolveDynamicCommand(device, command);
      if (cmd == DYNAMIC_COMMAND_NOT_FOUND) {
        omote_log_w("Scene '%s': sequence command %s_%s not found, skipped\r\n", sceneName, device, command);
        continue;
      }
      out.push_back({false, cmd, 0});
    }
  }
}

static void runSequence(const std::vector<SequenceStep>& steps) {
  if (steps.empty()) return;
  for (const SequenceStep& step : steps) {
    if (step.isSleep) {
      delay(step.durationMs);
    } else {
      executeCommand(step.command);
    }
  }
  // a long sequence must not send the remote to sleep right after it finished
  setLastActivityTimestamp_HAL();
}

// --- Dynamic Scene Registration ---
static void register_dynamic_scene(const std::string& sceneName, DynamicScene& scene) {
  const char* name = sceneName.c_str();

  // key mapping: { "<button>": {"device": "...", "command": "..."} } or { "<button>": {"scene": "..."} }
  String filePath = String("/scene_") + name + ".json";
  JsonDocument sceneDoc;
  if (loadJsonFile(filePath.c_str(), sceneDoc)) {
    for (JsonPairConst kv : sceneDoc.as<JsonObjectConst>()) {
      const char* keyName = kv.key().c_str();
      char key = getKeyCode(keyName);
      if (key == 0) {
        if (strcmp(keyName, "sequence") != 0) {
          omote_log_w("Scene '%s': unknown button '%s'\r\n", name, keyName);
        }
        continue;
      }
      JsonObjectConst cmdObj = kv.value().as<JsonObjectConst>();
      uint16_t cmd = DYNAMIC_COMMAND_NOT_FOUND;
      if (cmdObj["scene"].is<const char*>()) {
        auto target = dynamicScenes.find(cmdObj["scene"].as<const char*>());
        if (target != dynamicScenes.end()) {
          cmd = target->second.activateCommandForce;
        }
      } else {
        cmd = resolveDynamicCommand(cmdObj["device"] | "", cmdObj["command"] | "");
      }
      if (cmd == DYNAMIC_COMMAND_NOT_FOUND) {
        omote_log_w("Scene '%s': nothing found for button '%s', skipped\r\n", name, keyName);
        continue;
      }
      scene.keyCommandsShort[key] = cmd;
      omote_log_d("Scene '%s': button '%s' -> command %u\r\n", name, keyName, cmd);
    }
  } else {
    omote_log_w("Scene '%s' has no key mapping (%s)\r\n", name, filePath.c_str());
  }

  // extras: on/off sequences and the GUIs shown while the scene is active
  String extrasPath = String("/") + name + "_gui_extras.json";
  JsonDocument extrasDoc;
  if (loadJsonFile(extrasPath.c_str(), extrasDoc)) {
    parseSequence(extrasDoc["onSequence"].as<JsonArrayConst>(), scene.onSequence, name);
    parseSequence(extrasDoc["offSequence"].as<JsonArrayConst>(), scene.offSequence, name);
    for (JsonVariantConst gui : extrasDoc["selectedGuis"].as<JsonArrayConst>()) {
      const char* guiname = gui.as<const char*>();
      if (guiname == NULL) continue;
      std::string guiDisplayName = dynamicGuiDisplayName(guiname);
      if (registered_guis_byName_map.count(guiDisplayName) > 0) {
        scene.guiList.push_back(guiDisplayName);
      } else {
        omote_log_w("Scene '%s': GUI '%s' does not exist, skipped\r\n", name, guiname);
      }
    }
  }

  DynamicScene* s = &scene;
  register_scene(
    sceneName,
    [](){ /* keys are fully defined by the maps above */ },
    [s](){ runSequence(s->onSequence); },
    [s](){ runSequence(s->offSequence); },
    &scene.keyRepeatModes,
    &scene.keyCommandsShort,
    &scene.keyCommandsLong,
    scene.guiList.empty() ? NULL : &scene.guiList, // NULL: use the main GUI list
    scene.activateCommand
  );
  omote_log_i("Registered scene '%s': %u keys, %u/%u sequence steps, %u GUIs\r\n", name,
              scene.keyCommandsShort.size(), scene.onSequence.size(), scene.offSequence.size(), scene.guiList.size());
}

// Reads the master scenes file ("/scenes.json") and registers each dynamic scene.
void register_dynamic_scenes() {
  JsonDocument scenesDoc;
  if (!loadJsonFile("/scenes.json", scenesDoc)) {
    return;
  }

  // the config app writes {"scenes": [...]}, a plain array is accepted too
  JsonArrayConst scenesArray = scenesDoc["scenes"].as<JsonArrayConst>();
  if (scenesArray.isNull()) {
    scenesArray = scenesDoc.as<JsonArrayConst>();
  }
  if (scenesArray.isNull()) {
    omote_log_e("Invalid scenes.json format: 'scenes' array not found.\r\n");
    return;
  }

  // Pass 1: give every scene its activation commands, so that keys can switch to scenes listed later
  std::vector<std::string> sceneNames;
  for (JsonVariantConst sceneNameVariant : scenesArray) {
    const char* sceneName = sceneNameVariant.as<const char*>();
    if ((sceneName == NULL) || (sceneName[0] == '\0') || (dynamicScenes.count(sceneName) > 0)) continue;
    DynamicScene& scene = dynamicScenes[sceneName];
    register_command(&scene.activateCommand,      makeCommandData(SCENE, {sceneName}));
    register_command(&scene.activateCommandForce, makeCommandData(SCENE, {sceneName, "FORCE"}));
    sceneNames.push_back(sceneName);
  }

  // Pass 2: key mappings, sequences and GUI lists
  for (const std::string& sceneName : sceneNames) {
    register_dynamic_scene(sceneName, dynamicScenes[sceneName]);
  }
}
