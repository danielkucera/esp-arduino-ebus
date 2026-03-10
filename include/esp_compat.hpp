#pragma once

#include <cstdint>

class ESPClass {
 public:
  uint64_t getEfuseMac();
  const char* getChipModel();
  uint32_t getChipRevision();
  uint32_t getFlashChipSize();
  uint32_t getFlashChipSpeed();
  uint8_t getFlashChipMode();
  const char* getSdkVersion();
  uint32_t getFreeHeap();
  void restart();
};

extern ESPClass ESP;

uint32_t getCpuFrequencyMhz();
uint32_t getApbFrequency();
