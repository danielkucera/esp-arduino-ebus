#include "ebus_accessor.hpp"

#if defined(EBUS_INTERNAL)

static ebus::EbusConfig s_config;
static ebus::Controller s_controller;

ebus::EbusConfig& getEbusConfig() { return s_config; }
ebus::Controller& getEbusController() { return s_controller; }

void configureEbus(const ebus::EbusConfig& cfg) {
  s_config = cfg;
  s_controller.configure(s_config);
}

void startEbus() { s_controller.start(); }
void stopEbus() { s_controller.stop(); }

#endif