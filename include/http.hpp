#pragma once

#include <esp_http_server.h>

void SetupHttpHandlers();
void SetupHttpFallbackHandlers();
httpd_handle_t GetHttpServer();
