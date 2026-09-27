#pragma once

// Mock Mqtt for host testing - no ESP-IDF dependency.
// Only the static façade used by non-MQTT translation units. MqttHA tests
// inject lambdas directly (see mqtt_ha_tests.cpp); no recording needed here.

#include <freertos/FreeRTOS.h>

#include <ebus/detail/json_writer.hpp>
#include <string>
#include <string_view>

#include "config/app_config.hpp"

class Mqtt {
 public:
  bool isConnected() const { return false; }
  uint32_t getPublishedCount() const { return 0; }
  uint32_t getPublishFailedCount() const { return 0; }
  uint32_t getQueueDropCount() const { return 0; }
  uint32_t getConnectCount() const { return 0; }
  TaskHandle_t getTaskHandle() const { return nullptr; }
  static void publishComponentDiscovery() {}
  static void publishValue(std::string_view) {}
  static void publishError(int) {}
};

void appendMqttStatus(ebus::detail::JsonWriter& writer,
                      const AppConfig::Mqtt& mqtt_config);
