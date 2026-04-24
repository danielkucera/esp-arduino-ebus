#if defined(EBUS_INTERNAL)
/*
#include "DeviceManager.hpp"

#include <set>

DeviceManager deviceManager;

void DeviceManager::setEbusHandler(ebus::Handler* handler) {
  ebusHandler = handler;
}

void DeviceManager::collectData(const std::vector<uint8_t>& master,
                                const std::vector<uint8_t>& slave) {
  // Addresses
  masters[master[0]]++;
  if (ebus::isSlave(master[1])) slaves[master[1]]++;

  // Devices
  if (master[1] == ebusHandler->getTargetAddress()) return;
  if (ebus::isSlave(master[1])) devices[master[1]].update(master, slave);
}

void DeviceManager::resetAddresses() {
  masters.clear();
  slaves.clear();
}

const std::string DeviceManager::getDevicesJson() {
  cJSON* doc = cJSON_CreateArray();

  for (const auto& device : devices) {
    cJSON* item = cJSON_Parse(device.second.toJson().c_str());
    if (item != nullptr) cJSON_AddItemToArray(doc, item);
  }

  char* printed = cJSON_PrintUnformatted(doc);
  std::string payload = printed != nullptr ? printed : "[]";
  if (printed != nullptr) cJSON_free(printed);
  cJSON_Delete(doc);

  return payload;
}

const std::vector<const Device*> DeviceManager::getDevices() const {
  std::vector<const Device*> result;
  for (const auto& device : devices) {
    result.push_back(&(device.second));
  }
  return result;
}

void DeviceManager::populateMasterAddresses(cJSON* jsonObject) const {
  if (!cJSON_IsObject(jsonObject)) return;
  for (const auto& master : masters)
    cJSON_AddNumberToObject(jsonObject, ebus::to_string(master.first).c_str(),
                            master.second);
}

void DeviceManager::populateSlaveAddresses(cJSON* jsonObject) const {
  if (!cJSON_IsObject(jsonObject)) return;
  for (const auto& slave : slaves)
    cJSON_AddNumberToObject(jsonObject, ebus::to_string(slave.first).c_str(),
                            slave.second);
}

const std::vector<std::vector<uint8_t>> DeviceManager::scanCommands() const {
  std::set<uint8_t> scanSlaves;

  for (const auto& master : masters)
    if (master.first != ebusHandler->getSourceAddress())
      scanSlaves.insert(ebus::slaveOf(master.first));

  for (const auto& slave : slaves)
    if (slave.first != ebusHandler->getTargetAddress())
      scanSlaves.insert(slave.first);

  std::vector<std::vector<uint8_t>> result;
  for (const uint8_t slave : scanSlaves)
    result.push_back(Device::createScanCommand(slave));

  return result;
}

const std::vector<std::vector<uint8_t>> DeviceManager::vendorScanCommands()
    const {
  std::vector<std::vector<uint8_t>> result;
  for (const auto& device : devices) {
    const auto commands = device.second.createVendorScanCommands();
    if (!commands.empty())
      result.insert(result.end(), commands.begin(), commands.end());
  }
  return result;
}

const std::vector<std::vector<uint8_t>> DeviceManager::addressesScanCommands(
    const std::vector<std::string>& addresses) const {
  std::set<uint8_t> scanSlaves;
  for (const std::string& address : addresses) {
    const std::vector<uint8_t> bytes = ebus::to_vector(address);
    if (bytes.empty()) continue;
    uint8_t firstByte = bytes[0];
    if (ebus::isSlave(firstByte) &&
        firstByte != ebusHandler->getTargetAddress())
      scanSlaves.insert(firstByte);
  }
  std::vector<std::vector<uint8_t>> result;
  for (const uint8_t slave : scanSlaves)
    result.push_back(Device::createScanCommand(slave));
  return result;
}

void DeviceManager::setFullScan(bool enable) { fullScan = enable; }

bool DeviceManager::getFullScan() const { return fullScan; }

void DeviceManager::resetFullScan() { fullScanIndex = 0; }

bool DeviceManager::hasNextFullScan() const { return fullScanIndex < 0xff; }

std::vector<uint8_t> DeviceManager::nextFullScanCommand() {
  while (fullScanIndex < 0xff) {
    fullScanIndex++;
    if (ebus::isSlave(fullScanIndex) &&
        fullScanIndex != ebusHandler->getTargetAddress()) {
      return Device::createScanCommand(fullScanIndex);
    }
  }
  return {};
}

void DeviceManager::setScanOnStartup(bool enable) { scanOnStartup = enable; }

bool DeviceManager::getScanOnStartup() const { return scanOnStartup; }

void DeviceManager::resetStartupScan() { startupScanIndex = 0; }

bool DeviceManager::hasNextStartupScan() const {
  return startupScanIndex < maxStartupScans;
}

std::vector<uint8_t> DeviceManager::nextStartupScanCommand() {
  if (startupScanIndex < maxStartupScans) {
    startupScanIndex++;
    const auto cmds = scanCommands();
    if (startupScanIndex - 1 < cmds.size()) return cmds[startupScanIndex - 1];
  }
  return {};
}
*/
#endif
