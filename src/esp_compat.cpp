#include "esp_compat.hpp"

#include <cstring>

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_private/esp_clk.h>

namespace {

const char* chipModelToString(esp_chip_model_t model) {
  switch (model) {
    case CHIP_ESP32:
      return "ESP32";
    case CHIP_ESP32S2:
      return "ESP32-S2";
    case CHIP_ESP32S3:
      return "ESP32-S3";
    case CHIP_ESP32C3:
      return "ESP32-C3";
    case CHIP_ESP32C2:
      return "ESP32-C2";
    case CHIP_ESP32C6:
      return "ESP32-C6";
    case CHIP_ESP32H2:
      return "ESP32-H2";
#if defined(CHIP_ESP32C5)
    case CHIP_ESP32C5:
      return "ESP32-C5";
#endif
    default:
      return "ESP32";
  }
}

}  // namespace

ESPClass ESP;

uint64_t ESPClass::getEfuseMac() {
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint64_t value = 0;
  for (int i = 0; i < 6; ++i) {
    value = (value << 8) | mac[i];
  }
  return value;
}

const char* ESPClass::getChipModel() {
  esp_chip_info_t info{};
  esp_chip_info(&info);
  return chipModelToString(info.model);
}

uint32_t ESPClass::getChipRevision() {
  esp_chip_info_t info{};
  esp_chip_info(&info);
  return info.revision;
}

uint32_t ESPClass::getFlashChipSize() {
  uint32_t size = 0;
  if (esp_flash_default_chip != nullptr) {
    if (esp_flash_get_size(esp_flash_default_chip, &size) != ESP_OK) {
      size = 0;
    }
  }
  return size;
}

uint32_t ESPClass::getFlashChipSpeed() {
  return 0;
}

uint8_t ESPClass::getFlashChipMode() {
  return 0;
}

const char* ESPClass::getSdkVersion() { return esp_get_idf_version(); }

uint32_t ESPClass::getFreeHeap() { return esp_get_free_heap_size(); }

void ESPClass::restart() { esp_restart(); }

uint32_t getCpuFrequencyMhz() { return esp_clk_cpu_freq() / 1000000U; }

uint32_t getApbFrequency() { return esp_clk_apb_freq(); }
