#include "adapter_version.hpp"

#include <esp_efuse.h>

#include <cstdio>
#include <string>

static constexpr size_t adapter_hw_version_efuse_bits = 8;
static constexpr size_t adapter_hw_version_efuse_offset =
    248;  // BLOCK3 bit 248..255

static const esp_efuse_desc_t adapter_hw_version_efuse_desc = {
    EFUSE_BLK3, adapter_hw_version_efuse_offset, adapter_hw_version_efuse_bits};
static const esp_efuse_desc_t* adapter_hw_version_efuse_field[] = {
    &adapter_hw_version_efuse_desc, nullptr};

static uint8_t adapterHwVersionRaw = 0xEE;
static std::string adapterHwVersion = "unread";

static std::string formatAdapterHwVersion(const uint8_t raw) {
  if (static_cast<AdapterHwVersionEfuse>(raw) ==
      AdapterHwVersionEfuse::PRE_7_0) {
    return "pre-7.0";
  }

  const uint8_t major = (raw >> 4) & 0x0F;
  const uint8_t minor = raw & 0x0F;
  if (major <= 9 && minor <= 9) {
    char tmp[8];
    snprintf(tmp, sizeof(tmp), "%u.%u", major, minor);
    return std::string(tmp);
  }
  char tmp[8]{};
  snprintf(tmp, sizeof(tmp), "0x%02X", raw);
  return std::string(tmp);
}

void loadAdapterHwVersionFromEfuse() {
  uint8_t raw;
  const esp_err_t err = esp_efuse_read_field_blob(
      adapter_hw_version_efuse_field, &raw, adapter_hw_version_efuse_bits);
  if (err != ESP_OK) {
    adapterHwVersionRaw = 0xEE;
    adapterHwVersion = "reading error";
    return;
  }

  adapterHwVersionRaw = raw;
  adapterHwVersion = formatAdapterHwVersion(raw);
}

uint8_t getAdapterHwVersionRaw() { return adapterHwVersionRaw; }

const std::string& getAdapterHwVersionString() { return adapterHwVersion; }

std::pair<uint8_t, uint8_t> getAdapterSwVersion() { return {0x07, 0x02}; }
