# Profile Definitions (`profiles/`)

This directory contains compile-time definitions for **Data Profiles** and **Home Assistant (HA) Profiles**. 

At build time, Python scripts (`scripts/generate_data_profiles.py` and `scripts/generate_ha_profiles.py`) convert these JSON files into static C++ headers (`include/data_profile_gen.hpp` and `include/ha_profile_gen.hpp`) with zero heap allocation overhead.

---

## 1. Data Profiles (`data_profiles.json`)

Data profiles define eBUS data types, unit strings, display precision, divider scaling, and min/max ranges.

### Example Profile Entry
```json
{
  "name": "data2c_celsius",
  "datatype": "DATA2C",
  "unit": "°C",
  "divider": 1.0,
  "digits": 1,
  "min": -50.0,
  "max": 180.0
}
```

### Fields
- **`name`**: Profile identifier referenced in command field definitions (e.g. `"data2c_celsius"`).
- **`datatype`**: eBUS protocol data type (e.g. `DATA2C`, `DATA2B`, `UINT8`, `UINT16`, `UINT32`, `INT16`, `CHAR1`).
- **`unit`**: Display unit string (e.g. `"°C"`, `"bar"`, `"kW"`, `"kWh"`, `"%"`).
- **`divider`**: Value scaling divider applied during decoding (`decoded_value = raw / divider`).
- **`digits`**: Number of decimal places for floating point formatting (aligned with ebusd `@step` annotations: `@step(0.5)` → 1 digit, integer types → 0 digits).
- **`min` / `max`**: Default valid numerical bounds for write validation. Can be overridden per-field in command definitions.

---

## 2. Home Assistant Profiles (`ha_profiles.json`)

HA profiles specify Home Assistant MQTT discovery parameters (component category, device class, state class, entity category, key-value mappings).

### Example Profile Entry
```json
{
  "name": "sensor_temperature",
  "component": "sensor",
  "device_class": "temperature",
  "entity_category": "",
  "mode": "auto",
  "state_class": "measurement",
  "step": 0.5
}
```

### Key-Value Mappings (Enums / Selects)
Profiles can define discrete state/enum mappings for Home Assistant `select` or `sensor` components:
```json
{
  "name": "sensor_enum_state",
  "component": "sensor",
  "device_class": "enum",
  "state_class": "measurement",
  "key_value_pairs": [
    [0, "Ok"],
    [1, "Error"]
  ]
}
```

---

## 3. User Overlays (`*_user.json`)

You can define custom or overridden profiles without modifying the core files by using the user overlay files:
- `data_profiles_user.json`
- `ha_profiles_user.json`

During compilation, entries in the user overlay files override or extend base profiles in `data_profiles.json` and `ha_profiles.json`.
