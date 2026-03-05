#pragma once

#include <esp_http_server.h>

void SetupHttpHandlers();
httpd_handle_t GetHttpServer();
