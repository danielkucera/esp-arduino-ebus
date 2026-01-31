#if defined(EBUS_INTERNAL)
#include "Device.hpp"

#include <Ebus.h>

#include <map>
#include <string>

constexpr uint8_t VENDOR_VAILLANT = 0xb5;

// Manufacturer codes
const std::map<uint8_t, std::string> manufacturers = {
    {0x06, "Dungs"},    {0x0f, "FH Ostfalia"},   {0x10, "TEM"},
    {0x11, "Lamberti"}, {0x14, "CEB"},           {0x15, "Landis-Staefa"},
    {0x16, "FERRO"},    {0x17, "MONDIAL"},       {0x18, "Wikon"},
    {0x19, "Wolf"},     {0x20, "RAWE"},          {0x30, "Satronic"},
    {0x40, "ENCON"},    {0x50, "Kromschroeder"}, {0x60, "Eberle"},
    {0x65, "EBV"},      {0x75, "Graesslin"},     {0x85, "ebm-papst"},
    {0x95, "SIG"},      {0xa5, "Theben"},        {0xa7, "Thermowatt"},
    {0xb5, "Vaillant"}, {0xc0, "Toby"},          {0xc5, "Weishaupt"},
    {0xfd, "ebusd.eu"}};

// Identification (Service 07h 04h)
const std::vector<uint8_t> VEC_070400 = {0x07, 0x04, 0x00};

// Vaillant identification (Service B5h 09h 24h-27h)
const std::vector<uint8_t> VEC_b5090124 = {0xb5, 0x09, 0x01, 0x24};
const std::vector<uint8_t> VEC_b5090125 = {0xb5, 0x09, 0x01, 0x25};
const std::vector<uint8_t> VEC_b5090126 = {0xb5, 0x09, 0x01, 0x26};
const std::vector<uint8_t> VEC_b5090127 = {0xb5, 0x09, 0x01, 0x27};

const uint8_t& Device::getSlave() const { return slave; }

void Device::update(const std::vector<uint8_t>& master,
                    const std::vector<uint8_t>& slave) {
  this->slave = master[1];
  if (ebus::contains(master, VEC_070400, 2))
    vec_070400 = slave;
  else if (ebus::contains(master, VEC_b5090124, 2))
    vec_b5090124 = slave;
  else if (ebus::contains(master, VEC_b5090125, 2))
    vec_b5090125 = slave;
  else if (ebus::contains(master, VEC_b5090126, 2))
    vec_b5090126 = slave;
  else if (ebus::contains(master, VEC_b5090127, 2))
    vec_b5090127 = slave;
}

JsonDocument Device::toJson() const {
  JsonDocument doc;

  uint8_t master = ebus::masterOf(slave);
  doc["master"] = master != slave ? ebus::to_string(master) : "";
  doc["slave"] = ebus::to_string(slave);
  doc["manufacturer"] = ebus::range(vec_070400, 1, 1).size() > 0
                            ? manufacturers.at(vec_070400[1])
                            : "";
  doc["unitid"] = ebus::byte_2_char(ebus::range(vec_070400, 2, 5));
  doc["software"] = ebus::to_string(ebus::range(vec_070400, 7, 2));
  doc["hardware"] = ebus::to_string(ebus::range(vec_070400, 9, 2));

  if (isVaillant() && isVaillantValid()) {
    std::string serial = ebus::byte_2_char(ebus::range(vec_b5090124, 2, 8));
    serial += ebus::byte_2_char(ebus::range(vec_b5090125, 1, 9));
    serial += ebus::byte_2_char(ebus::range(vec_b5090126, 1, 9));
    serial += ebus::byte_2_char(ebus::range(vec_b5090127, 1, 2));

    // doc["prefix"] = serial.substr(0, 2);
    // doc["year"] = serial.substr(2, 2);
    // doc["week"] = serial.substr(4, 2);
    doc["product"] = serial.substr(6, 10);
    // doc["supplier"] = serial.substr(16, 4);
    // doc["counter"] = serial.substr(20, 6);
    // doc["suffix"] = serial.substr(26, 2);
  }

  doc["ebusd"] = ebusdConfiguration();

  doc.shrinkToFit();
  return doc;
}

const std::vector<uint8_t> Device::scanCommand(const uint8_t& slave) {
  std::vector<uint8_t> command = {slave};
  command.insert(command.end(), VEC_070400.begin(), VEC_070400.end());
  return command;
}

const std::vector<std::vector<uint8_t>> Device::scanCommandsVendor() const {
  std::vector<std::vector<uint8_t>> commands;
  if (isVaillant()) {
    if (vec_b5090124.size() == 0) {
      std::vector<uint8_t> command = {slave};
      command.insert(command.end(), VEC_b5090124.begin(), VEC_b5090124.end());
      commands.push_back(command);
    }
    if (vec_b5090125.size() == 0) {
      std::vector<uint8_t> command = {slave};
      command.insert(command.end(), VEC_b5090125.begin(), VEC_b5090125.end());
      commands.push_back(command);
    }
    if (vec_b5090126.size() == 0) {
      std::vector<uint8_t> command = {slave};
      command.insert(command.end(), VEC_b5090126.begin(), VEC_b5090126.end());
      commands.push_back(command);
    }
    if (vec_b5090127.size() == 0) {
      std::vector<uint8_t> command = {slave};
      command.insert(command.end(), VEC_b5090127.begin(), VEC_b5090127.end());
      commands.push_back(command);
    }
  }
  return commands;
}

bool Device::isVaillant() const {
  return (vec_070400.size() > 1 && vec_070400[1] == VENDOR_VAILLANT);
}

bool Device::isVaillantValid() const {
  return (vec_b5090124.size() > 0 && vec_b5090125.size() > 0 &&
          vec_b5090126.size() > 0 && vec_b5090127.size() > 0);
}

std::string Device::ebusdConfiguration() const {
  // Format: ZZ.CCC[CC]*

  // ZZ: slave address in hex, padded to 2 digits
  std::string conf = ebus::to_string(slave);

  // CCC[CC]: unitid from identification
  std::string unitid = ebus::byte_2_char(ebus::range(vec_070400, 2, 5));

  // remove non-alphanumeric characters
  unitid.erase(std::remove_if(unitid.begin(), unitid.end(),
                              [](char c) { return !std::isalnum(c); }),
               unitid.end());
  // If length > 3, remove up to 2 trailing '0' characters
  while (unitid.length() > 3 && unitid.back() == '0') {
    unitid.pop_back();
  }
  // transform to lowercase
  std::transform(unitid.begin(), unitid.end(), unitid.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  // add unitid to conf if not empty
  if (unitid.length() > 0)
    conf += "." + unitid + "*";
  else
    conf += ".*";

  return conf;
}

#endif