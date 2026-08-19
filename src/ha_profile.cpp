#if defined(EBUS_INTERNAL)
#include "ha_profile.hpp"

#include "ha_profile_gen.hpp"

const HAProfile* findHAProfile(std::string_view name) {
  for (const auto& p : profiles) {
    if (name == p.name) return &p;
  }
  return nullptr;
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
