#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <cstdint>

struct stat {
  uint64_t st_size;
  uint32_t st_mode;
  uint32_t st_mtime;
};

inline int stat(const char* path, struct stat* buf) { return -1; }
inline int remove(const char* path) { return -1; }

#ifdef __cplusplus
}
#endif
