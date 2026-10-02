#pragma once

#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>

typedef struct in_addr ip4_addr_t;
typedef struct in_addr esp_ip4_addr_t;

inline int lwip_ioctl(int fd, long request, void* value) {
  return ioctl(fd, request, value);
}
