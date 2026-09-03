#pragma once

#include <esp_http_server.h>

#include <ebus/types.hpp>
#include <string>

class ConfigManager {
 public:
  static void begin();
  static void resetConfig();
  static std::string_view readString(const char* key,
                                     const char* fallback = "");
  static int32_t readInt(const char* key, int32_t fallback = 0);
  static bool readBool(const char* key, bool fallback = false);
  static bool writeString(const char* key, const std::string& value);

  static esp_err_t handleGet(httpd_req_t* req);
  static esp_err_t handleSet(httpd_req_t* req);
  static esp_err_t handleReset(httpd_req_t* req);

  static void fetchConfig(const ebus::JsonChunkVisitor& visitor);

 private:
  static bool writeConfigJson(std::string_view body, std::string& error);
};

extern ConfigManager configManager;
