#pragma once

#include <WiFiClient.h>

int pushEnhClient(WiFiClient* client, uint8_t c, uint8_t d, bool log);
void handleEnhClient(WiFiClient* client);

