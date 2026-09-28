#include <cstring>

#include "esp_efuse.h"
#include "esp_mac.h"
#include "identity_stub_state.hpp"

namespace HostIdentityStub {
namespace {
std::array<uint8_t, 6> mac{};
esp_err_t mac_read_result = ESP_OK;
int last_mac_type = -1;
uint8_t adapter_version_raw = 0;
esp_err_t adapter_version_read_result = ESP_OK;
}  // namespace

void reset() {
  mac = {};
  mac_read_result = ESP_OK;
  last_mac_type = -1;
  adapter_version_raw = 0;
  adapter_version_read_result = ESP_OK;
}

void setMac(const std::array<uint8_t, 6>& value) { mac = value; }

void setMacReadResult(esp_err_t result) { mac_read_result = result; }

int lastMacType() { return last_mac_type; }

void setAdapterVersionRaw(uint8_t raw) { adapter_version_raw = raw; }

void setAdapterVersionReadResult(esp_err_t result) {
  adapter_version_read_result = result;
}
}  // namespace HostIdentityStub

esp_err_t esp_read_mac(uint8_t* dst, int type) {
  HostIdentityStub::last_mac_type = type;
  if (HostIdentityStub::mac_read_result == ESP_OK) {
    std::memcpy(dst, HostIdentityStub::mac.data(),
                HostIdentityStub::mac.size());
  }
  return HostIdentityStub::mac_read_result;
}

esp_err_t esp_efuse_read_field_blob(const esp_efuse_desc_t** field, void* dst,
                                    size_t dst_size_bits) {
  (void)field;
  if (HostIdentityStub::adapter_version_read_result == ESP_OK &&
      dst_size_bits >= 8) {
    *static_cast<uint8_t*>(dst) = HostIdentityStub::adapter_version_raw;
  }
  return HostIdentityStub::adapter_version_read_result;
}
