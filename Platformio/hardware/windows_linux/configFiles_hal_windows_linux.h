#pragma once

#include <stddef.h>
#include <string>
#include <vector>

/*
  The simulator keeps the files of the config app in a folder on the PC instead of SPIFFS:
  "./simulator_config" (relative to the folder the simulator is started in, like the other
  simulator assets), or the folder set in the environment variable OMOTE_CONFIG_DIR.
  If the folder does not exist, it is created with the example configuration from
  "./hardware/windows_linux/config_example".
*/

// called by hardwarePresenter.cpp. Returns false if the file does not exist.
bool readConfigFile_HAL(const std::string& filename, std::string& content);

// used by the simulated web server
struct ConfigFileInfo {
  std::string name;
  size_t size;
};
const std::string& configDir_simulator();
bool configFileExists_simulator(const std::string& filename);
bool writeConfigFile_simulator(const std::string& filename, const std::string& content);
bool deleteConfigFile_simulator(const std::string& filename);
std::vector<ConfigFileInfo> listConfigFiles_simulator();
