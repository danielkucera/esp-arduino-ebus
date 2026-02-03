#pragma once

#if defined(EBUS_INTERNAL)
#include <ArduinoJson.h>

#include <cstdint>
#include <vector>

// Represents a device on the eBUS, identified by its slave address and
// identification data. Provides methods to update its data and serialize it to
// JSON. Also provides static methods to generate scan commands for devices.
// Vendor-specific scan commands are also supported.

class Device {
 public:
  Device() = default;
  ~Device() = default;

  const uint8_t& getSlave() const;

  // Update stored identification vectors
  void update(const std::vector<uint8_t>& master,
              const std::vector<uint8_t>& slave);

  // Serialization
  JsonDocument toJson() const;

  // Scan commands
  static const std::vector<uint8_t> scanCommand(const uint8_t& slave);
  const std::vector<std::vector<uint8_t>> scanCommandsVendor() const;

  // Identification
  static const bool getIdentification(const std::vector<uint8_t>& master,
                                      std::vector<uint8_t>* const slave);

 private:
  uint8_t slave = 0;

  std::vector<uint8_t> vec_070400;

  std::vector<uint8_t> vec_b5090124;
  std::vector<uint8_t> vec_b5090125;
  std::vector<uint8_t> vec_b5090126;
  std::vector<uint8_t> vec_b5090127;

  bool isVaillant() const;
  bool isVaillantValid() const;

  std::string ebusdConfiguration() const;
};

#endif