#if defined(EBUS_INTERNAL)
#include "HaProfile.hpp"

namespace {
// clang-format off
constexpr HAProfile kProfiles[] = {
    {"sensor_temperature", "sensor", "temperature", "", "auto", "measurement", 0.5, 0, 0},
    {"sensor_humidity", "sensor", "humidity", "", "auto", "measurement", 0, 0, 0},
    {"sensor_pressure", "sensor", "pressure", "", "auto", "measurement", 0, 0, 0},
    {"sensor_flow", "sensor", "volume_flow_rate", "", "auto", "measurement", 0, 0, 0},
    {"sensor_power", "sensor", "power", "", "auto", "measurement", 0, 0, 0},
    {"sensor_energy", "sensor", "energy", "", "auto", "total_increasing", 0, 0, 0},
    {"sensor_rssi", "sensor", "signal_strength", "diagnostic", "auto", "measurement", 0, 0, 0},
    {"sensor_voltage", "sensor", "voltage", "", "auto", "measurement", 0, 0, 0},
    {"sensor_current", "sensor", "current", "", "auto", "measurement", 0, 0, 0},
    {"sensor_frequency", "sensor", "frequency", "", "auto", "measurement", 0, 0, 0},
    {"sensor_enum", "sensor", "enum", "", "auto", "measurement", 0, 0, 0},
    {"sensor_total", "sensor", "", "", "auto", "total", 0, 0, 0},
    {"binary_sensor", "binary_sensor", "", "", "auto", "", 0, 1, 0},
    {"binary_sensor_running", "binary_sensor", "running", "", "auto", "", 0, 1, 0},
    {"switch", "switch", "", "config", "auto", "", 0, 1, 0},
    {"number_temperature", "number", "temperature", "", "box", "", 0.5, 0, 0},
    {"select_mode", "select", "", "", "auto", "", 0, 0, 0},
    {"select_enum", "select", "enum", "config", "auto", "", 0, 0, 0},
    {"button", "button", "", "config", "auto", "", 0, 0, 0},
};
// clang-format on
}  // namespace

const HAProfile* findHAProfile(std::string_view name) {
  for (const auto& p : kProfiles) {
    if (name == p.name) return &p;
  }
  return nullptr;
}

#endif
