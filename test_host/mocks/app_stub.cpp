// Host stub for App: there is no live application object in host tests,
// so ConfigManager HTTP handlers fall back to the legacy direct-NVS path
// (see ConfigManager::handleSet/handleReset).

#include "app/app.hpp"
#include "system/device_status.hpp"

App* App::instance_ = nullptr;

App* App::instance() { return nullptr; }

bool App::loadConfig() { return false; }

bool App::applyFlatConfigJson(std::string_view body, std::string& error) {
  (void)body;
  error = "no App instance on host";
  return false;
}

// No live snapshot on host: drift tests compare AppConfig structs
// directly (see app_config_tests), the handler is ESP-only.
const AppConfig& DeviceStatus::config() {
  static AppConfig defaults;
  return defaults;
}
