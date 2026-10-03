#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#if defined(WIN32)
  #include <direct.h>
#endif
#include "configFiles_hal_windows_linux.h"

static const char* DEFAULT_CONFIG_DIR = "./simulator_config";
static const char* EXAMPLE_CONFIG_DIR = "./hardware/windows_linux/config_example";

static bool isDirectory(const std::string& path) {
  struct stat st;
  return (stat(path.c_str(), &st) == 0) && S_ISDIR(st.st_mode);
}

static bool makeDirectory(const std::string& path) {
  #if defined(WIN32)
  return _mkdir(path.c_str()) == 0;
  #else
  return mkdir(path.c_str(), 0755) == 0;
  #endif
}

static bool endsWith(const std::string& s, const std::string& suffix) {
  return (s.size() >= suffix.size()) && (s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
}

static std::vector<std::string> jsonFilesIn(const std::string& dir) {
  std::vector<std::string> names;
  DIR* d = opendir(dir.c_str());
  if (d == NULL) return names;
  while (struct dirent* entry = readdir(d)) {
    std::string name = entry->d_name;
    if (endsWith(name, ".json")) {
      names.push_back(name);
    }
  }
  closedir(d);
  return names;
}

static bool readFile(const std::string& path, std::string& content) {
  std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
  if (!in) return false;
  std::ostringstream buffer;
  buffer << in.rdbuf();
  content = buffer.str();
  return true;
}

static bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream out(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out << content;
  return out.good();
}

const std::string& configDir_simulator() {
  static std::string configDir;
  if (configDir.empty()) {
    const char* fromEnv = getenv("OMOTE_CONFIG_DIR");
    configDir = (fromEnv != NULL && fromEnv[0] != '\0') ? fromEnv : DEFAULT_CONFIG_DIR;
    while (configDir.size() > 1 && (configDir.back() == '/' || configDir.back() == '\\')) {
      configDir.pop_back();
    }
    if (!isDirectory(configDir)) {
      if (!makeDirectory(configDir)) {
        printf("Config files: cannot create folder %s\r\n", configDir.c_str());
      } else {
        // start with something to look at
        int copied = 0;
        for (const std::string& name : jsonFilesIn(EXAMPLE_CONFIG_DIR)) {
          std::string content;
          if (readFile(std::string(EXAMPLE_CONFIG_DIR) + "/" + name, content) && writeFile(configDir + "/" + name, content)) {
            copied++;
          }
        }
        printf("Config files: created %s with %d example files\r\n", configDir.c_str(), copied);
      }
    }
    printf("Config files: %s\r\n", configDir.c_str());
  }
  return configDir;
}

bool readConfigFile_HAL(const std::string& filename, std::string& content) {
  return readFile(configDir_simulator() + "/" + filename, content);
}

bool configFileExists_simulator(const std::string& filename) {
  struct stat st;
  return stat((configDir_simulator() + "/" + filename).c_str(), &st) == 0;
}

bool writeConfigFile_simulator(const std::string& filename, const std::string& content) {
  return writeFile(configDir_simulator() + "/" + filename, content);
}

bool deleteConfigFile_simulator(const std::string& filename) {
  if (!configFileExists_simulator(filename)) return true;
  return remove((configDir_simulator() + "/" + filename).c_str()) == 0;
}

std::vector<ConfigFileInfo> listConfigFiles_simulator() {
  std::vector<ConfigFileInfo> files;
  for (const std::string& name : jsonFilesIn(configDir_simulator())) {
    struct stat st;
    if (stat((configDir_simulator() + "/" + name).c_str(), &st) == 0) {
      files.push_back({name, (size_t)st.st_size});
    }
  }
  return files;
}
