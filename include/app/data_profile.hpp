#pragma once

#if defined(EBUS_INTERNAL)

#include <ebus/types.hpp>
#include <string>

struct DataProfile {
  const char* name;
  const char* datatype;
  const char* unit;
  float divider;
  uint8_t digits;
  float min;
  float max;
};

const DataProfile* findDataProfile(std::string_view name);
const DataProfile* getProfileByIndex(uint8_t idx);  // 1-based index
uint8_t getProfileIndex(const DataProfile* p);      // Returns 1-based index

#endif
