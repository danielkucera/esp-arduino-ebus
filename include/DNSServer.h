#pragma once

#include <cstdint>
#include <string>

#include "IPAddress.h"

class DNSServer {
 public:
  DNSServer();
  ~DNSServer();

  bool start(uint16_t port, const char* domainName, const IPAddress& resolvedIp);
  void processNextRequest();
  void stop();

 private:
  int socketFd_ = -1;
  uint16_t port_ = 0;
  std::string domain_;
  IPAddress resolvedIp_;
};
