#pragma once

#if defined(EBUS_INTERNAL)

#include <ebus/types.hpp>
#include <string>

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
};

const HAProfile* findHAProfile(std::string_view name);

#endif
