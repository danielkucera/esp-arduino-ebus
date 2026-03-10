#pragma once

#include <esp_http_server.h>

void SetupHttpHandlers();
void SetupHttpFallbackHandlers();
httpd_handle_t GetHttpServer();
bool RegisterUri(const char* uri, httpd_method_t method,
                 esp_err_t (*handler)(httpd_req_t*), void* user_ctx = nullptr);
