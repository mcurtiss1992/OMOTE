#pragma once

#include <string>

// Mounts SPIFFS, where the config app stores its files. Safe to call several times.
bool mountConfigFS_HAL();
// Reads "<filename>" from SPIFFS. Returns false if the file does not exist.
bool readConfigFile_HAL(const std::string& filename, std::string& content);
