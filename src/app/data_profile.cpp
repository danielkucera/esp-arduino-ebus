#if defined(EBUS_INTERNAL)
#include "app/data_profile.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "app/data_profile_gen.hpp"
#include "system/logger.hpp"

const DataProfile* findDataProfile(std::string_view name) {
  auto it = std::find_if(std::begin(profiles), std::end(profiles),
                         [&](const DataProfile& p) { return name == p.name; });
  if (it == std::end(profiles)) {
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "DataProfile: not found: %.*s",
                  static_cast<int>(std::min<size_t>(name.size(), 96)),
                  name.data());
    logger.warn(buffer);
    return nullptr;
  }
  return &*it;
}

const DataProfile* getProfileByIndex(uint8_t idx) {
  if (idx == 0 || idx > std::size(profiles)) return nullptr;
  return &profiles[idx - 1];
}

uint8_t getProfileIndex(const DataProfile* p) {
  if (!p) return 0;
  // profiles array is in anonymous namespace, iterate to find index
  for (uint8_t i = 0; i < std::size(profiles); i++) {
    if (&profiles[i] == p) return i + 1;
  }
  return 0;
}

#endif
