#pragma once

#include <cstdint>
#include <string>

class IPAddress {
 public:
  IPAddress();
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d);

  bool fromString(const char* value);
  std::string toString() const;

  uint32_t toU32() const;
  void fromU32(uint32_t value);

 private:
  uint8_t bytes_[4];
};
