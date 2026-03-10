#pragma once

#include <Arduino.h>
#include <esp_http_server.h>

#include "HttpUtils.hpp"

class ConfigManager {
 public:
  void begin();
  void resetConfig();
  String readString(const char* key, const char* fallback = "");
  int32_t readInt(const char* key, int32_t fallback = 0);
  bool readBool(const char* key, bool fallback = false);
  bool writeString(const char* key, const String& value);

 private:
  String readConfigJson();
  bool writeConfigJson(const String& body, String& error);

  esp_err_t handleGet(httpd_req_t* req);
  esp_err_t handleSet(httpd_req_t* req);
  esp_err_t handleReset(httpd_req_t* req);

  template <typename T, esp_err_t (T::*Method)(httpd_req_t*)>
  friend esp_err_t HttpUtils::Trampoline(httpd_req_t* req);
};
