#include <freertos/FreeRTOS.h>

#include "app/mqtt.hpp"
#include "network/captive_dns.hpp"
#include "network/http_utils.hpp"
#include "network/sntp.hpp"
#include "network/wifi_network_manager.hpp"
#include "system/adapter_version.hpp"
#include "system/device_identity.hpp"
#include "system/device_status.hpp"
#include "system/esp_ota_manager.hpp"
#include "system/logger.hpp"
#include "system/system_monitor.hpp"

uint32_t DeviceStatus::resetCode() { return 0; }

Mqtt& DeviceStatus::mqtt() {
  static Mqtt mqtt;
  return mqtt;
}

EspOtaManager& DeviceStatus::espOtaManager() {
  static EspOtaManager manager;
  return manager;
}

int32_t WifiNetworkManager::RSSI() { return -50; }

int WifiNetworkManager::getReconnectCount() { return 0; }
TaskHandle_t WifiNetworkManager::getStatusLedTaskHandle() { return nullptr; }

SystemMonitor& DeviceStatus::monitor() {
  static SystemMonitor monitor;
  return monitor;
}

void SystemMonitor::fetchTap(const ebus::JsonChunkVisitor& visitor,
                             uint64_t since_wall_ms) const {
  (void)visitor;
  (void)since_wall_ms;
}

size_t SystemMonitor::fetchHeapTrend(HeapSample* out, size_t capacity) const {
  (void)out;
  (void)capacity;
  return 0;
}

TaskHandle_t SystemMonitor::task_handle() const { return nullptr; }

void SystemMonitor::getSocketStatus(int& detected, int& connected) {
  detected = 0;
  connected = 0;
}

TaskHandle_t getCaptiveDnsTaskHandle() { return nullptr; }

const char* getUniqueId() { return "host-test"; }
uint8_t getAdapterHwVersionRaw() { return 0; }

const std::string& getAdapterHwVersionString() {
  static const std::string version = "host-test";
  return version;
}

void appendWifiStatus(ebus::detail::JsonWriter& writer) {
  writer.writeField("connected", false);
}

void appendMqttStatus(ebus::detail::JsonWriter& writer,
                      const AppConfig::Mqtt& mqtt_config) {
  (void)mqtt_config;
  writer.writeField("connected", false);
}

void appendSntpStatus(ebus::detail::JsonWriter& writer,
                      const AppConfig::Sntp& sntp_config) {
  (void)sntp_config;
  writer.writeField("enabled", false);
}

bool HttpUtils::prepareJsonReaderForArray(ebus::detail::JsonReader& reader,
                                          std::string_view expected_array_key,
                                          std::string& error_out) {
  auto token = reader.next();
  if (token == ebus::detail::JsonReader::Token::object_start) {
    if (!reader.findKey(expected_array_key)) {
      error_out = "JSON object must contain a '" +
                  std::string(expected_array_key) + "' key.";
      return false;
    }
    token = reader.next();
  } else if (token != ebus::detail::JsonReader::Token::array_start) {
    error_out = "JSON root must be an object or a direct array.";
    return false;
  }
  if (token != ebus::detail::JsonReader::Token::array_start) {
    error_out = "Expected a JSON array.";
    return false;
  }
  return true;
}
