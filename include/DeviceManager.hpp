#pragma once

#if defined(EBUS_INTERNAL)

#include <Ebus.h>

#include <cstdint>
#include <map>

#include "Device.hpp"

// Manages devices on the eBUS, identified by their slave address and
// identification data. Collects data from eBUS messages to identify devices and
// their manufacturers. Provides methods to retrieve device information in JSON
// format and generate scan commands for discovered devices. Supports full bus
// scans and startup scans. Also tracks master and slave addresses observed on
// the bus.

class DeviceManager {
 public:
  void setEbusHandler(ebus::Handler* handler);

  void collectData(const std::vector<uint8_t>& master,
                   const std::vector<uint8_t>& slave);

  void resetAddresses();

  const std::string getDevicesJson();
  const std::vector<const Device*> getDevices() const;

  void populateMasterAddresses(JsonObject& jsonObject) const;
  void populateSlaveAddresses(JsonObject& jsonObject) const;

  const std::vector<std::vector<uint8_t>> scanCommands() const;
  const std::vector<std::vector<uint8_t>> vendorScanCommands() const;

  void setFullScan(bool enable);
  bool getFullScan() const;

  void resetFullScan();
  bool hasNextFullScan() const;
  std::vector<uint8_t> nextFullScanCommand();

  void setScanOnStartup(bool enable);
  bool getScanOnStartup() const;

  void resetStartupScan();
  bool hasNextStartupScan() const;
  std::vector<uint8_t> nextStartupScanCommand();

 private:
  ebus::Handler* ebusHandler = nullptr;

  std::map<uint8_t, Device> devices;
  std::map<uint8_t, uint32_t> masters;
  std::map<uint8_t, uint32_t> slaves;

  bool fullScan = false;
  uint8_t fullScanIndex = 0;

  bool scanOnStartup = false;
  uint8_t startupScanIndex = 0;
  uint8_t maxStartupScans = 5;
};

extern DeviceManager deviceManager;
#endif