#pragma once

#include <array>
#include <cstdint>

#include "esp_system.h"

namespace HostIdentityStub {

void reset();
void setMac(const std::array<uint8_t, 6>& mac);
void setMacReadResult(esp_err_t result);
int lastMacType();
void setAdapterVersionRaw(uint8_t raw);
void setAdapterVersionReadResult(esp_err_t result);

}  // namespace HostIdentityStub
