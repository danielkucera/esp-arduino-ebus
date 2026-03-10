#include "WiFiServer.h"

#include <cerrno>

#include <lwip/sockets.h>
#include <fcntl.h>
#include <unistd.h>

WiFiServer::WiFiServer(uint16_t port) : port_(port) {}

WiFiServer::~WiFiServer() { closeListenSocket(); }

void WiFiServer::begin() { createListenSocket(); }

bool WiFiServer::hasClient() {
  if (pendingClientFd_ >= 0) return true;
  if (listenFd_ < 0 && !createListenSocket()) return false;

  sockaddr_in addr{};
  socklen_t addrLen = sizeof(addr);
  int client =
      ::accept(listenFd_, reinterpret_cast<sockaddr*>(&addr), &addrLen);
  if (client < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) return false;
    return false;
  }
  pendingClientFd_ = client;
  return true;
}

WiFiClient WiFiServer::accept() {
  if (!hasClient()) return WiFiClient();
  int client = pendingClientFd_;
  pendingClientFd_ = -1;
  return WiFiClient(client);
}

bool WiFiServer::createListenSocket() {
  if (listenFd_ >= 0) return true;

  listenFd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listenFd_ < 0) return false;

  int enable = 1;
  setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    closeListenSocket();
    return false;
  }

  if (listen(listenFd_, 4) != 0) {
    closeListenSocket();
    return false;
  }

  int flags = fcntl(listenFd_, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(listenFd_, F_SETFL, flags | O_NONBLOCK);
  }

  return true;
}

void WiFiServer::closeListenSocket() {
  if (pendingClientFd_ >= 0) {
    close(pendingClientFd_);
    pendingClientFd_ = -1;
  }
  if (listenFd_ >= 0) {
    close(listenFd_);
    listenFd_ = -1;
  }
}
