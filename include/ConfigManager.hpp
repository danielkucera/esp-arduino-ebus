#pragma once

#include <WebServer.h>

class ConfigManager {
 public:
  void begin(WebServer* server);

 private:
  String readConfigJson();
  bool writeConfigJson(const String& body, String& error);

  void handleGet();
  void handleSet();

  WebServer* server_ = nullptr;
};
