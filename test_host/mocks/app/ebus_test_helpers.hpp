#pragma once

#include "app/ebus_accessor.hpp"

inline bool configureHostEbusController() {
  auto& controller = getEbusController();
  if (controller.isConfigured()) return true;

  auto config = getEbusConfig();
  config.bus.device = "/dev/null";
  return controller.configure(config);
}
