#include "theme.h"
#include "platform.h"
#include "logger.h"
#include <sys/stat.h>
#include <fstream>
#include <iomanip>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

ThemeConfig *ThemeConfig::instance{NULL};

ThemeConfig::ThemeConfig() {
}

ThemeConfig *ThemeConfig::get_instance() {
  if (instance == NULL) {
    instance = new ThemeConfig();
  }
  return instance;
}

void ThemeConfig::init(const std::string config_path) {
  LOG_INFO("Theme path is: {}", config_path);
  path = config_path;
  struct stat buffer;

  if (stat(config_path.c_str(), &buffer) == 0) {
    try {
      data = json::parse(std::ifstream(config_path));
    } catch (const std::exception &e) {
      // A present-but-corrupt theme (truncated / empty / partial OTA write)
      // must not abort boot before a single pixel is drawn. Fall back to the
      // same defaults used when the file is missing.
      LOG_ERROR("Theme file {} is not valid JSON ({}); using defaults", config_path, e.what());
      data = {
        {"primary_color", "0x2196F3"},
        {"secondary_color", "0xF44336"}
      };
    }
  } else {
    LOG_ERROR("Theme file not found: {}", config_path);
    data = {
      {"primary_color", "0x2196F3"}, //blue
      {"secondary_color", "0xF44336"} // red
    };
  }
}

