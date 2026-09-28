#pragma once

// Host mock for lwIP sockets.h: only declarations needed to parse headers
// that reference socket address types (no socket calls run on host).

#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>

typedef struct in_addr ip4_addr_t;
typedef struct in_addr esp_ip4_addr_t;
