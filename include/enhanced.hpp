#ifndef INCLUDE_ENHANCED_HPP_
#define INCLUDE_ENHANCED_HPP_

#include <WiFiClient.h>

int pushEnhClient(WiFiClient* client, uint8_t c, uint8_t d, bool log);
void handleEnhClient(WiFiClient* client);

#endif  // INCLUDE_ENHANCED_HPP_
