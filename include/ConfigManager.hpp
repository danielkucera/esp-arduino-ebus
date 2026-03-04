#pragma once

#include <WebServer.h>

class ConfigManager {
 public:
  void begin(WebServer* server);
  void resetConfig();
  String readString(const char* key, const char* fallback = "");
  int32_t readInt(const char* key, int32_t fallback = 0);
  bool readBool(const char* key, bool fallback = false);
  bool writeString(const char* key, const String& value);

 private:
  String readConfigJson();
  bool writeConfigJson(const String& body, String& error);

  void handleGet();
  void handleSet();
  void handleReset();

  WebServer* server_ = nullptr;
};
