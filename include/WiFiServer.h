#pragma once

#include <cstdint>

#include "WiFiClient.h"

class WiFiServer {
 public:
  explicit WiFiServer(uint16_t port);
  WiFiServer(const WiFiServer&) = delete;
  WiFiServer& operator=(const WiFiServer&) = delete;
  ~WiFiServer();

  void begin();
  bool hasClient();
  WiFiClient accept();

 private:
  bool createListenSocket();
  void closeListenSocket();

  uint16_t port_;
  int listenFd_ = -1;
  int pendingClientFd_ = -1;
};
