#if defined(EBUS_INTERNAL)
#include "data_profile.hpp"

#include <algorithm>
#include <cstring>

#include "data_profile_gen.hpp"
#include "logger.hpp"

const DataProfile* findDataProfile(std::string_view name) {
  auto it = std::find_if(std::begin(profiles), std::end(profiles),
                         [&](const DataProfile& p) { return name == p.name; });
  if (it == std::end(profiles)) {
    logger.warn("DataProfile: not found: " + std::string(name));
    return nullptr;
  }
  return &*it;
}

const DataProfile* getProfileByIndex(uint8_t idx) {
  if (idx == 0 || idx > 33) return nullptr;
  return &profiles[idx - 1];
}

uint8_t getProfileIndex(const DataProfile* p) {
  if (!p) return 0;
  // profiles array is in anonymous namespace, iterate to find index
  for (uint8_t i = 0; i < 33; i++) {
    if (&profiles[i] == p) return i + 1;
  }
  return 0;
}

#endif
