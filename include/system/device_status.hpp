#pragma once

#include <cstdint>
#include <ebus/types.hpp>

struct AppConfig;

#if defined(EBUS_INTERNAL)
class SystemMonitor;
class Mqtt;
class MqttHA;
#endif
class EspOtaManager;

/**
 * Device status model: gathers and serializes the full device status JSON.
 * Consumed by all status publishers (HTTP status API, MQTT telemetry).
 */
class DeviceStatus {
 public:
  static void setConfig(const AppConfig* config);
  static void setResetCode(uint32_t code);
  static void setEspOtaManager(EspOtaManager* manager);
#if defined(EBUS_INTERNAL)
  static void setMqtt(Mqtt* mqtt);
  static void setMqttHa(MqttHA* mqtt_ha);
#endif
#if defined(EBUS_INTERNAL)
  static void setMonitor(SystemMonitor* monitor);
#endif

  static const AppConfig& config();
  static uint32_t resetCode();
  static EspOtaManager& espOtaManager();
#if defined(EBUS_INTERNAL)
  static Mqtt& mqtt();
  static MqttHA& mqttHa();
#endif
#if defined(EBUS_INTERNAL)
  static SystemMonitor& monitor();
#endif

  static void fetchStatus(const ebus::JsonChunkVisitor& visitor);

 private:
  static const AppConfig* config_;
  static uint32_t reset_code_;
  static EspOtaManager* esp_ota_manager_;
#if defined(EBUS_INTERNAL)
  static Mqtt* mqtt_;
  static MqttHA* mqtt_ha_;
  static SystemMonitor* monitor_;
#endif
};
