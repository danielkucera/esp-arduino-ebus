#pragma once

#include <cstdint>

#include "esp_system.h"

enum { ESP_MAC_WIFI_STA = 0 };

esp_err_t esp_read_mac(uint8_t* mac, int type);
