#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <new>
#include <string>
#include "configFiles_hal_esp32.h"
#include "applicationInternal/omote_log.h"

#define FORMAT_SPIFFS_IF_FAILED true

bool mountConfigFS_HAL() {
  static bool mounted = false;
  if (!mounted) {
    mounted = SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED);
    if (!mounted) {
      omote_log_e("SPIFFS Mount Failed\r\n");
    }
  }
  return mounted;
}

bool readConfigFile_HAL(const std::string& filename, std::string& content) {
  std::string path = "/" + filename;
  if (!mountConfigFS_HAL() || !SPIFFS.exists(path.c_str())) {
    return false;
  }
  File file = SPIFFS.open(path.c_str(), FILE_READ);
  if (!file || file.isDirectory()) {
    omote_log_e("Failed to open file for reading: %s\r\n", path.c_str());
    return false;
  }
  // one read() for the whole file, reading it byte by byte is a lot slower
  try {
    content.resize(file.size());
  } catch (const std::bad_alloc&) {
    omote_log_e("Not enough memory to read %s (%u bytes)\r\n", path.c_str(), file.size());
    file.close();
    return false;
  }
  size_t bytesRead = file.read((uint8_t*)&content[0], content.size());
  content.resize(bytesRead);
  file.close();
  return true;
}
