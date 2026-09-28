#include "system/device_status.hpp"

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_private/esp_clk.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ebus/detail/json_writer.hpp>
#include <ebus/status.hpp>

#include "app/app_limits.hpp"
#include "app/mqtt.hpp"
#include "app/mqtt_ha.hpp"
#include "config/app_config.hpp"
#include "main.hpp"
#include "network/captive_dns.hpp"
#include "network/sntp.hpp"
#include "network/wifi_network_manager.hpp"
#include "system/adapter_version.hpp"
#include "system/device_identity.hpp"
#include "system/esp_ota_manager.hpp"
#include "system/logger.hpp"
#include "system/system_monitor.hpp"

#if !defined(EBUS_INTERNAL)
#include "bridge/bus_type.hpp"
#else
#include "app/command_manager.hpp"
#include "app/cron.hpp"
#endif

const AppConfig* DeviceStatus::config_ = nullptr;
uint32_t DeviceStatus::reset_code_ = 0;
EspOtaManager* DeviceStatus::esp_ota_manager_ = nullptr;
#if defined(EBUS_INTERNAL)
SystemMonitor* DeviceStatus::monitor_ = nullptr;
Mqtt* DeviceStatus::mqtt_ = nullptr;
MqttHA* DeviceStatus::mqtt_ha_ = nullptr;
#endif

namespace {

#if defined(EBUS_INTERNAL)
const AppConfig& statusConfig() { return DeviceStatus::config(); }
#endif

struct StatusInfo {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    writer.writeField("reset_code", DeviceStatus::resetCode());
    writer.writeField("uptime",
                      static_cast<uint32_t>(esp_timer_get_time() / 1000ULL));
  }
};

struct HeapStatus {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    writer.writeField("total_free_bytes", info.total_free_bytes);
    writer.writeField("largest_free_block", info.largest_free_block);
    writer.writeField("minimum_free_bytes", info.minimum_free_bytes);
    writer.writeField("free_blocks", info.free_blocks);
    writer.writeField("total_blocks", info.total_blocks);
  }
};

#if !defined(EBUS_INTERNAL)
struct ArbitrationInfo {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    writer.writeField("total", static_cast<int>(Bus.nbr_arbitrations_));
    writer.writeField("restarts1", static_cast<int>(Bus.nbr_restarts_1_));
    writer.writeField("restarts2", static_cast<int>(Bus.nbr_restarts_2_));
    writer.writeField("won1", static_cast<int>(Bus.nbr_won_1_));
    writer.writeField("won2", static_cast<int>(Bus.nbr_won_2_));
    writer.writeField("lost1", static_cast<int>(Bus.nbr_lost_1_));
    writer.writeField("lost2", static_cast<int>(Bus.nbr_lost_2_));
    writer.writeField("late", static_cast<int>(Bus.nbr_late_));
    writer.writeField("errors", static_cast<int>(Bus.nbr_errors_));
  }
};
#endif

struct FirmwareStatus {
  void toJson(ebus::detail::JsonWriter& writer) const {
    auto scope = writer.objectScope();
    writer.writeField("version", AUTO_VERSION);
    writer.writeField("build", __DATE__ " " __TIME__);
    writer.writeField("esp_idf_version", esp_get_idf_version());
#if !defined(EBUS_INTERNAL)
    writer.writeField("async", static_cast<bool>(USE_ASYNCHRONOUS));
    writer.writeField("software_serial",
                      static_cast<bool>(USE_SOFTWARE_SERIAL));
#endif
    writer.writeField("unique_id", getUniqueId());
    writer.writeField("adapter_hw_version", getAdapterHwVersionString());
    writer.writeField("adapter_hw_version_raw", getAdapterHwVersionRaw());
    writer.writeField("clock_speed", esp_clk_cpu_freq() / 1000000U);
    writer.writeField("apb_speed", esp_clk_apb_freq());
  }
};

struct ChipStatus {
  static void toJson(ebus::detail::JsonWriter& writer) {
    auto scope = writer.objectScope();
    esp_chip_info_t chip_info{};
    esp_chip_info(&chip_info);
    uint32_t flash_size = 0;
    if (esp_flash_default_chip != nullptr)
      esp_flash_get_size(esp_flash_default_chip, &flash_size);
    writer.writeField("chip_revision", static_cast<int>(chip_info.revision));
    writer.writeField("flash_size", flash_size);
  }
};

#if defined(EBUS_INTERNAL)
struct EbusStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    w.writeField("pwm", statusConfig().pwm.value);
    w.writeField("ebus_address", statusConfig().bus.address.c_str());
    w.writeField("bus_window", statusConfig().bus.window_us);
    w.writeField("bus_offset", statusConfig().bus.offset_us);
  }
};

struct ScheduleStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    w.writeField("system_inquiry", statusConfig().bus.system_inquiry);
    w.writeField("system_response", statusConfig().bus.system_response);
    w.writeField("scan_on_startup", statusConfig().bus.scan_on_startup);
    w.writeField("active_commands",
                 static_cast<uint32_t>(commandManager.getActiveCommands()));
    w.writeField("passive_commands",
                 static_cast<uint32_t>(commandManager.getPassiveCommands()));
  }
};

struct HaStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    w.writeField("enabled", DeviceStatus::mqttHa().isEnabled());
  }
};

struct SocketsStatus {
  static void toJson(ebus::detail::JsonWriter& w) {
    auto obj_scope = w.objectScope();
    int detected = 0;
    int connected = 0;
    DeviceStatus::monitor().getSocketStatus(detected, connected);
    w.writeField("detected", detected);
    w.writeField("connected", connected);
    w.writeField("max", CONFIG_LWIP_MAX_SOCKETS);
  }
};
#endif

}  // namespace

void DeviceStatus::setConfig(const AppConfig* config) { config_ = config; }

void DeviceStatus::setResetCode(uint32_t code) { reset_code_ = code; }

const AppConfig& DeviceStatus::config() { return *config_; }

uint32_t DeviceStatus::resetCode() { return reset_code_; }

void DeviceStatus::setEspOtaManager(EspOtaManager* manager) {
  esp_ota_manager_ = manager;
}

EspOtaManager& DeviceStatus::espOtaManager() { return *esp_ota_manager_; }

#if defined(EBUS_INTERNAL)
void DeviceStatus::setMonitor(SystemMonitor* monitor) { monitor_ = monitor; }

SystemMonitor& DeviceStatus::monitor() { return *monitor_; }

void DeviceStatus::setMqtt(Mqtt* mqtt) { mqtt_ = mqtt; }

Mqtt& DeviceStatus::mqtt() { return *mqtt_; }

void DeviceStatus::setMqttHa(MqttHA* mqtt_ha) { mqtt_ha_ = mqtt_ha; }

MqttHA& DeviceStatus::mqttHa() { return *mqtt_ha_; }
#endif

void DeviceStatus::fetchStatus(const ebus::JsonChunkVisitor& visitor) {
  ebus::detail::JsonWriter writer(visitor);
  auto scope = writer.objectScope();
  writer.writeField("status", StatusInfo{});
  writer.writeField("heap", HeapStatus{});

#if !defined(EBUS_INTERNAL)
  writer.writeField("arbitration", ArbitrationInfo{});
#endif
  writer.writeField("firmware", FirmwareStatus{});
  writer.writeField("chip", ChipStatus{});
  {
    auto wifi_scope = writer.objectScope("wifi");
    appendWifiStatus(writer);
  }

#if defined(EBUS_INTERNAL)
  {
    auto sntp_scope = writer.objectScope("sntp");
    appendSntpStatus(writer, statusConfig().sntp);
  }
  writer.writeField("ebus", EbusStatus{});
  writer.writeField("schedule", ScheduleStatus{});
  {
    auto mqtt_scope = writer.objectScope("mqtt");
    appendMqttStatus(writer, statusConfig().mqtt);
  }
  writer.writeField("home_assistant", HaStatus{});
  writer.writeField("sockets", SocketsStatus{});
#endif
}
