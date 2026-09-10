#pragma once

#include <string>

#include "config/app_config.hpp"
#include "config_manager.hpp"

class AppConfigLoader {
 public:
  AppConfigLoader(ConfigManager& config_manager)
      : config_manager_(config_manager) {}

  /**
   * @brief Loads all configuration from NVS into the provided AppConfig.
   * @param config The AppConfig to populate.
   * @return true on success, false if NVS is not ready.
   */
  bool load(AppConfig& config);

  /**
   * @brief Saves the AppConfig back to NVS, writing only changed keys.
   * @param config The AppConfig to persist.
   * @return true on success, false on error.
   */
  bool save(const AppConfig& config);

 private:
  ConfigManager& config_manager_;
};