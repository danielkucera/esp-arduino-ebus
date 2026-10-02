#pragma once

#include <netinet/in.h>
#include <cstdint>

typedef struct esp_netif_obj esp_netif_t;
typedef struct in_addr esp_ip4_addr_t;
typedef struct {
  esp_ip4_addr_t ip;
  esp_ip4_addr_t netmask;
  esp_ip4_addr_t gw;
} esp_netif_ip_info_t;
typedef const char* esp_event_base_t;
