#if defined(EBUS_INTERNAL)
#include "ha_profile.hpp"

#include <algorithm>

#include "ha_profile_gen.hpp"

const HAProfile* findHAProfile(std::string_view name) {
  auto it = std::find_if(std::begin(profiles), std::end(profiles),
                         [&](const HAProfile& p) { return name == p.name; });
  return it != std::end(profiles) ? &*it : nullptr;
}

const HAProfile* getHAProfileByIndex(uint8_t idx) {
  if (idx == 0 || idx > 21) return nullptr;
  return &profiles[idx - 1];
}

uint8_t getProfileIndexHA(const HAProfile* p) {
  if (!p) return 0;
  for (uint8_t i = 0; i < 21; i++) {
    if (&profiles[i] == p) return i + 1;
  }
  return 0;
}

#endif
