#if defined(EBUS_INTERNAL)
#include "ha_profile.hpp"

#include <algorithm>
#include <iterator>

#include "ha_profile_gen.hpp"

const HAProfile* findHAProfile(std::string_view name) {
  auto it = std::find_if(std::begin(profiles), std::end(profiles),
                         [&](const HAProfile& p) { return name == p.name; });
  return it != std::end(profiles) ? &*it : nullptr;
}

const HAProfile* getHAProfileByIndex(uint8_t idx) {
  if (idx == 0 || idx > std::size(profiles)) return nullptr;
  return &profiles[idx - 1];
}

uint8_t getProfileIndexHA(const HAProfile* p) {
  if (!p) return 0;
  for (uint8_t i = 0; i < std::size(profiles); i++) {
    if (&profiles[i] == p) return i + 1;
  }
  return 0;
}

#endif
