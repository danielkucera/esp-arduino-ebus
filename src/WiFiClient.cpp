#include "WiFiClient.h"

#include <cerrno>
#include <cstring>

#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <unistd.h>

WiFiClient::WiFiClient() = default;

WiFiClient::WiFiClient(int socketFd) : socketFd_(socketFd) {}

WiFiClient::WiFiClient(WiFiClient&& other) noexcept
    : socketFd_(other.socketFd_) {
  other.socketFd_ = -1;
}

WiFiClient& WiFiClient::operator=(WiFiClient&& other) noexcept {
  if (this == &other) return *this;
  closeSocket();
  socketFd_ = other.socketFd_;
  other.socketFd_ = -1;
  return *this;
}

WiFiClient::~WiFiClient() { closeSocket(); }

WiFiClient::operator bool() const { return connected(); }

bool WiFiClient::connected() const {
  if (socketFd_ < 0) return false;
  char buffer;
  int result = recv(socketFd_, &buffer, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result > 0) return true;
  if (result == 0) return false;
  if (errno == EWOULDBLOCK || errno == EAGAIN) return true;
  return false;
}

void WiFiClient::stop() { closeSocket(); }

int WiFiClient::available() const {
  if (socketFd_ < 0) return 0;
  int pending = 0;
  if (lwip_ioctl(socketFd_, FIONREAD, &pending) == 0) {
    return pending;
  }
  return 0;
}

int WiFiClient::availableForWrite() const { return connected() ? 1 : 0; }

int WiFiClient::read() {
  uint8_t byte = 0;
  int result = recv(socketFd_, &byte, 1, MSG_DONTWAIT);
  if (result <= 0) return -1;
  return byte;
}

int WiFiClient::peek() {
  uint8_t byte = 0;
  int result = recv(socketFd_, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result <= 0) return -1;
  return byte;
}

int WiFiClient::read(uint8_t* buffer, size_t size) {
  if (buffer == nullptr || size == 0) return 0;
  int result = recv(socketFd_, buffer, size, MSG_DONTWAIT);
  if (result < 0) return 0;
  return result;
}

size_t WiFiClient::write(uint8_t byte) {
  return write(&byte, 1);
}

size_t WiFiClient::write(const uint8_t* buffer, size_t size) {
  if (socketFd_ < 0 || buffer == nullptr || size == 0) return 0;
  int result = send(socketFd_, buffer, size, 0);
  if (result < 0) return 0;
  return static_cast<size_t>(result);
}

size_t WiFiClient::write(const char* buffer) {
  if (buffer == nullptr) return 0;
  return write(reinterpret_cast<const uint8_t*>(buffer), std::strlen(buffer));
}

void WiFiClient::flush() {}

void WiFiClient::setNoDelay(bool enable) {
  if (socketFd_ < 0) return;
  int flag = enable ? 1 : 0;
  setsockopt(socketFd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

size_t WiFiClient::print(const char* value) {
  if (value == nullptr) return 0;
  return write(reinterpret_cast<const uint8_t*>(value), std::strlen(value));
}

size_t WiFiClient::print(const std::string& value) {
  return write(reinterpret_cast<const uint8_t*>(value.c_str()), value.size());
}

size_t WiFiClient::println(const char* value) {
  size_t written = print(value);
  static const char kNewline[] = "\r\n";
  written += write(reinterpret_cast<const uint8_t*>(kNewline), 2);
  return written;
}

void WiFiClient::closeSocket() {
  if (socketFd_ >= 0) {
    shutdown(socketFd_, SHUT_RDWR);
    close(socketFd_);
    socketFd_ = -1;
  }
}
