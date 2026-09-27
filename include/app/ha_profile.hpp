#pragma once

#if defined(EBUS_INTERNAL)

#include <array>
#include <ebus/types.hpp>
#include <string>
#include <utility>

inline constexpr size_t max_key_value_pairs = 5;

struct HAProfile {
  const char* name;
  const char* component;
  const char* device_class;
  const char* entity_category;
  const char* mode;
  const char* state_class;
  float step;
  uint8_t payload_on;
  uint8_t payload_off;
  // Key-value pairs for select/sensor_enum components
  // Empty means no key-value mapping
  std::array<std::pair<int, const char*>, max_key_value_pairs> key_value_pairs;
  size_t key_value_count;
  int default_key;
};

const HAProfile* findHAProfile(std::string_view name);
const HAProfile* getHAProfileByIndex(uint8_t idx);
uint8_t getHaProfileIndex(const HAProfile* p);

#endif
