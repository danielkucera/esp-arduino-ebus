#pragma once

#include <cstdlib>
#include <cstring>

#define MALLOC_CAP_INTERNAL 0x1
#define MALLOC_CAP_8BIT 0x2

typedef struct {
  size_t total_free_bytes;
  size_t largest_free_block;
  size_t minimum_free_bytes;
} multi_heap_info_t;

inline void heap_caps_get_info(multi_heap_info_t* info, uint32_t) {
  if (info == nullptr) return;
  info->total_free_bytes = 1024;
  info->largest_free_block = 512;
  info->minimum_free_bytes = 256;
}

inline void* heap_caps_malloc(size_t size, uint32_t) {
  return std::malloc(size);
}
inline void heap_caps_free(void* ptr) { std::free(ptr); }
