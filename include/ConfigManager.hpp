#pragma once

#include <esp_http_server.h>

#include <string>

class ConfigManager {
 public:
  void begin();
  void resetConfig();
  std::string readString(const char* key, const char* fallback = "");
  int32_t readInt(const char* key, int32_t fallback = 0);
  bool readBool(const char* key, bool fallback = false);
  bool writeString(const char* key, const std::string& value);

  esp_err_t handleGet(httpd_req_t* req);
  esp_err_t handleSet(httpd_req_t* req);
  esp_err_t handleReset(httpd_req_t* req);

 private:
  std::string readConfigJson();
  bool writeConfigJson(const std::string& body, std::string& error);
};

extern ConfigManager configManager;
