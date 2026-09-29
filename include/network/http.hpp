#pragma once

#include <esp_http_server.h>

#if defined(EBUS_INTERNAL)
class MqttHA;

// MqttHA is passed explicitly (internal builds have Home Assistant);
// the bridge build has no HA service to inject.
void setupHttpHandlers(MqttHA& mqtt_ha);
#else
void setupHttpHandlers();
#endif
void setupHttpFallbackHandlers();
bool registerUri(const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*));
