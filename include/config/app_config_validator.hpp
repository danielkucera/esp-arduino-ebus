#pragma once

#include <ebus/detail/json_reader.hpp>
#include <ebus/types.hpp>
#include <string_view>

#include "config/app_config.hpp"

namespace config {

/**
 * Validates AppConfig values against protocol limits and logical constraints.
 * Mirrors ebus::detail::ConfigValidator for the application configuration
 * layer.
 */
class AppConfigValidator {
 public:
  static bool validate(const AppConfig& config);

  /**
   * @brief Performs schema-like validation of raw JSON before it is applied.
   * Verifies that numeric values fall within allowed ranges and that required
   * string fields are non-empty. Unknown keys are ignored.
   */
  static bool validateJson(std::string_view json);
};

}  // namespace config