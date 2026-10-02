#include "system/device_identity.hpp"

#include <esp_mac.h>
#include <inttypes.h>

#include <cstdint>
#include <cstdio>

namespace {

char unique_id[7]{};

uint64_t getEfuseMac() {
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  uint64_t value = 0;
  for (int i = 0; i < 6; ++i) {
    value = (value << 8) | mac[i];
  }
  return value;
}

}  // namespace

void calcUniqueId() {
  const uint32_t id = static_cast<uint32_t>(getEfuseMac() & 0xFFFFFFULL);
  snprintf(unique_id, sizeof(unique_id), "%06" PRIx32, id);
}

const char* getUniqueId() { return unique_id; }
