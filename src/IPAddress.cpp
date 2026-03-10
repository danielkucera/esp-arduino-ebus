#include "IPAddress.h"

#include <cstdio>

#include <arpa/inet.h>
#include <lwip/ip4_addr.h>

IPAddress::IPAddress() : bytes_{0, 0, 0, 0} {}

IPAddress::IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    : bytes_{a, b, c, d} {}

bool IPAddress::fromString(const char* value) {
  if (value == nullptr) return false;
  ip4_addr_t addr{};
  if (ip4addr_aton(value, &addr) == 0) return false;
  uint32_t ip = ntohl(addr.addr);
  bytes_[0] = (ip >> 24) & 0xFF;
  bytes_[1] = (ip >> 16) & 0xFF;
  bytes_[2] = (ip >> 8) & 0xFF;
  bytes_[3] = ip & 0xFF;
  return true;
}

std::string IPAddress::toString() const {
  char buffer[16]{};
  std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", bytes_[0], bytes_[1],
                bytes_[2], bytes_[3]);
  return std::string(buffer);
}

uint32_t IPAddress::toU32() const {
  uint32_t value = 0;
  value |= static_cast<uint32_t>(bytes_[0]) << 24;
  value |= static_cast<uint32_t>(bytes_[1]) << 16;
  value |= static_cast<uint32_t>(bytes_[2]) << 8;
  value |= static_cast<uint32_t>(bytes_[3]);
  return value;
}

void IPAddress::fromU32(uint32_t value) {
  bytes_[0] = (value >> 24) & 0xFF;
  bytes_[1] = (value >> 16) & 0xFF;
  bytes_[2] = (value >> 8) & 0xFF;
  bytes_[3] = value & 0xFF;
}
