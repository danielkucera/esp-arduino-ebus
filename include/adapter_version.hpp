#pragma once

#include <cstdint>
#include <string>
#include <utility>

enum class AdapterHwVersionEfuse : uint8_t {
  PRE_7_0 = 0x00,
  V7_0 = 0x70,
};

void loadAdapterHwVersionFromEfuse();
uint8_t getAdapterHwVersionRaw();
const std::string& getAdapterHwVersionString();
std::pair<uint8_t, uint8_t> getAdapterSwVersion();
