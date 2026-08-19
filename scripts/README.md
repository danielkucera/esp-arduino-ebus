# Scripts

## generate_data_profiles.py

Generates `include/data_profile_gen.hpp` — a constexpr array of `DataProfile` structs
from `config/data_profiles.json`. Run automatically as a PlatformIO pre-build hook.

## generate_ha_profiles.py

Generates `include/ha_profile_gen.hpp` — a constexpr array of `HAProfile` structs
from `config/ha_profiles.json`. Run automatically as a PlatformIO pre-build hook.

## migrate_commands.py

Migrates `commands.json` (and optionally `simulation.json`) from the old single-field
format with inline HA configuration to the new multi-field format using data profiles.

### Usage

```bash
# Migrate specific file(s) in-place
python3 scripts/migrate_commands.py commands.json

# Migrate with separate input and output
python3 scripts/migrate_commands.py commands.json.full commands_new.json

# No arguments: migrates commands.json and simulation.json in-place
python3 scripts/migrate_commands.py
```

### What it does

1. Converts flat field properties (`datatype`, `divider`, `min`, `max`, `digits`, `unit`)
   into a `fields` array referencing named data profiles from `config/data_profiles.json`.

2. Maps old inline HA configuration fields (`ha_component`, `ha_device_class`,
   `ha_state_class`, `ha_key_value_map`, `ha_payload_on`, `ha_payload_off`,
   `ha_entity_category`, `ha_mode`, `ha_step`) into HA profile references
   from `config/ha_profiles.json`.

3. Removes all old-format fields, producing clean JSON ready for the app.

### Old format example

```json
{
  "key": "01",
  "name": "Outside_Temperature",
  "read_cmd": "fe070009",
  "active": false,
  "interval": 0,
  "master": true,
  "position": 1,
  "datatype": "DATA2B",
  "divider": 1,
  "min": 0,
  "max": 0,
  "digits": 2,
  "unit": "°C",
  "ha": true,
  "ha_component": "sensor",
  "ha_device_class": "temperature",
  "ha_state_class": ""
}
```

### New format output

```json
{
  "key": "01",
  "name": "Outside_Temperature",
  "read_cmd": "fe070009",
  "write_cmd": "",
  "active": false,
  "interval": 0,
  "fields": [
    {
      "name": "value",
      "profile": "temperature_d2b",
      "position": 1,
      "master": true
    }
  ],
  "ha": true,
  "ha_profile": "sensor_temperature"
}
```
