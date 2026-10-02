#include <freertos/FreeRTOS.h>

#include <algorithm>
#include <ebus/detail/json_writer.hpp>

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

namespace HostSystemApiStub {
namespace {
SystemMonitor::HeapSample heap_trend[SystemMonitor::heap_trend_capacity]{};
size_t heap_trend_count = 0;
uint64_t tap_since_millis = 0;
}  // namespace

void setHeapTrend(const SystemMonitor::HeapSample* samples, size_t count) {
  heap_trend_count = std::min(count, SystemMonitor::heap_trend_capacity);
  for (size_t i = 0; i < heap_trend_count; ++i) heap_trend[i] = samples[i];
}

void clearHeapTrend() { heap_trend_count = 0; }

void resetTap() { tap_since_millis = 0; }

uint64_t tapSinceMillis() { return tap_since_millis; }

}  // namespace HostSystemApiStub

uint32_t DeviceStatus::resetCode() { return 0; }

Mqtt& DeviceStatus::mqtt() {
  static Mqtt mqtt;
  return mqtt;
}

EspOtaManager& DeviceStatus::espOtaManager() {
  static EspOtaManager manager;
  return manager;
}

int32_t WifiNetworkManager::rssi() { return -50; }

int WifiNetworkManager::getReconnectCount() { return 0; }
TaskHandle_t WifiNetworkManager::getStatusLedTaskHandle() { return nullptr; }

SystemMonitor& DeviceStatus::monitor() {
  static SystemMonitor monitor;
  return monitor;
}

void SystemMonitor::fetchTap(const ebus::JsonChunkVisitor& visitor,
                             uint64_t since_wall_ms) const {
  HostSystemApiStub::tap_since_millis = since_wall_ms;
  ebus::detail::JsonWriter writer(visitor);
  {
    auto root = writer.objectScope();
    {
      auto tap = writer.arrayScope("tap");
    }
    writer.writeField("dropped", 0);
    writer.writeField("capacity", 0);
  }
}

size_t SystemMonitor::fetchHeapTrend(HeapSample* out, size_t capacity) const {
  const size_t count = std::min(capacity, HostSystemApiStub::heap_trend_count);
  for (size_t i = 0; i < count; ++i) out[i] = HostSystemApiStub::heap_trend[i];
  return count;
}

TaskHandle_t SystemMonitor::taskHandle() const { return nullptr; }

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
