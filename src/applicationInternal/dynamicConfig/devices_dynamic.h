#ifndef DYNAMIC_DEVICE_REGISTRATION_H
#define DYNAMIC_DEVICE_REGISTRATION_H

#include <stdint.h>
#include <string>
#include <ArduinoJson.h>

/*
  Devices, scenes and GUIs can be defined in JSON files instead of in code, e.g. with the OMOTE Config app.
  The files are read once during startup. On the ESP32 they are stored in SPIFFS, in the simulator in a folder
  on the PC (see "hardware/windows_linux/configFiles_hal_windows_linux.cpp").
*/

// --- Configuration Constants ---
#define MAX_COMMANDS 200   // Limit to 200 commands
#define MAX_NAME_LEN 32    // Maximum length for a command name ("<device>_<command>", including the terminating 0)

// Returned by the lookup functions when no command is registered under that name.
// 0 cannot be used for this, because 0 is a valid command id.
#define DYNAMIC_COMMAND_NOT_FOUND 0xFFFF

// --- Command Table Structure ---
struct CommandEntry {
  char name[MAX_NAME_LEN]; // Command name (fixed size)
  uint16_t value;          // Command value (unique ID, etc.)
  bool inUse;              // True if the slot is occupied
};

// Preallocated table for command entries (defined in the .cpp file)
extern CommandEntry commandTable[MAX_COMMANDS];

// --- Function Prototypes ---

/**
 * @brief Reads a config file (e.g. "devices.json") and parses it into doc.
 *
 * The file is read in one go into a temporary buffer, which is a lot faster than
 * letting the parser pull it byte by byte, and the buffer is freed before returning.
 *
 * @return false if the file does not exist or is not valid JSON.
 */
bool loadJsonFile(const std::string& filename, JsonDocument& doc);

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
 * @param data IR: protocol number, MQTT: topic, BLE: peer address.
 * @param dataExtended IR: code, MQTT: payload, BLE: KEYBOARD_BLE_* key name.
 */
void register_command_dynamic(const char* name, const char* commandType, const char* data, const char* dataExtended);

/**
 * @brief Reads a device's JSON file and registers each command dynamically.
 *
 * The function reads a JSON file named in the format "device_<deviceName>.json", parses
 * the contained JSON array, and registers each command found.
 *
 * @param deviceName The name of the device.
 * @return true if the device commands were registered successfully, false otherwise.
 */
bool register_dynamic_device(const char *deviceName);

/**
 * @brief Clears the command table.
 *
 * This function resets all entries in the command table to a default state.
 */
void clearCommands();

/**
 * @brief Reads a master devices file and registers each dynamic device.
 *
 * The master file ("devices.json") is expected to be a JSON array of device names.
 * Has to be called after the BLE keyboard commands have been registered.
 */
void register_dynamic_devices();

/**
 * @brief Looks up a dynamically registered command by device and command name.
 *
 * @return The command id, or DYNAMIC_COMMAND_NOT_FOUND.
 */
uint16_t getCommandValue(const char* device, const char* command);

/**
 * @brief Maps a KEYBOARD_BLE_* name to the command id of the BLE keyboard key.
 *
 * @return The command id, or DYNAMIC_COMMAND_NOT_FOUND.
 */
uint16_t getBLECommandValue(const std::string& commandName);

/**
 * @brief Resolves a "device"/"command" pair as written by the config app.
 *
 * The pseudo device "BLE" addresses the built-in BLE keyboard keys.
 *
 * @return The command id, or DYNAMIC_COMMAND_NOT_FOUND.
 */
uint16_t resolveDynamicCommand(const char* device, const char* command);

/**
 * @brief Executes a configured command, used by the "Test" buttons of the config app.
 *
 * @return false if no such command is registered.
 */
bool executeDynamicCommand(std::string device, std::string command, std::string payload);

#endif // DYNAMIC_DEVICE_REGISTRATION_H
