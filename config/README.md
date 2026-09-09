# Configuration Files (`config/`)

This directory contains command configuration files for the eBUS adapter and virtual bus simulation.

## Command JSON Structure

Commands are defined as a JSON array of objects. Each command object specifies an eBUS command and its data fields.

### Example

```json
[
  {
    "key": "09",
    "name": "Buffer/Middle_Temperature",
    "read_cmd": "08b50903290100",
    "write_cmd": "",
    "interval": 60,
    "master": false,
    "fields": [
      {
        "name": "middle_temperature",
        "profile": "data2c_celsius",
        "position": 1,
        "ha_profile": "sensor_temperature"
      },
      {
        "name": "state",
        "profile": "uint8",
        "position": 3,
        "ha_profile": "sensor_enum_state"
      }
    ]
  },
  {
    "key": "32",
    "name": "Basement/Temperature_NightRoomSetPoint",
    "read_cmd": "50b509030d3300",
    "write_cmd": "50b509040e3300",
    "interval": 60,
    "master": false,
    "fields": [
      {
        "name": "value",
        "profile": "data1c_celsius",
        "position": 1,
        "ha_profile": "number_temperature",
        "min": 15,
        "max": 20
      }
    ]
  }
]
```

### Schema Description

#### Command Properties
- **`key`** *(string)*: Unique decimal or hexadecimal identifier for the command (e.g. `"09"` or `"43"`). Used as prefix for HA `unique_id` and MQTT control topic (`ebus/<id>/set/<key>`).
- **`name`** *(string)*: Command name hierarchy using slashes and underscores (e.g. `"Buffer/Middle_Temperature"`). Generates MQTT value subtopic `ebus/<id>/values/buffer/middle_temperature`.
- **`read_cmd`** *(hex string)*: Hexadecimal eBUS master read sequence (e.g. `"08b50903290100"`).
- **`write_cmd`** *(hex string, optional)*: Hexadecimal eBUS master write prefix sequence.
- **`interval`** *(integer)*: Automatic polling interval in seconds. `0` disables active polling (passive listening only).
- **`master`** *(boolean)*: `true` if field data is located in the master message payload; `false` if in the slave response payload.
- **`fields`** *(array)*: Array of data field definitions (max 4 per command).

#### Field Properties
- **`name`** *(string)*: Field identifier used as the key in MQTT value JSON payload (`{"middle_temperature": 49.3, "state": 85}`).
- **`profile`** *(string)*: Reference to a Data Profile in `profiles/data_profiles.json` (e.g. `"data2c_celsius"`, `"unit8"`).
- **`position`** *(integer)*: 1-based offset of the field within the eBUS message payload (default `1`).
- **`ha_profile`** *(string, optional)*: Reference to an HA Profile in `profiles/ha_profiles.json` (e.g. `"sensor_temperature"`). If omitted or empty, no Home Assistant entity is registered for this field.
- **`min`** *(float, optional)*: Override the profile's default min value for write validation (e.g. `15` for a setpoint with profile `data1c_celsius` default min=0).
- **`max`** *(float, optional)*: Override the profile's default max value for write validation (e.g. `20` for a setpoint with profile `data1c_celsius` default max=75).

## Files
- `simulation.json`: Command definitions used when building in simulation mode (`esp32-c3-internal-simulation`).
