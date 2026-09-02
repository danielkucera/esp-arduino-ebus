# Data Profile Mapping & App Status

### ebus library native DataTypes (from `ebus/include/ebus/data_types.hpp`)

| ebus library DataType | Size | Endianness | Notes |
|---|---|---|---|
| BCD | 1 byte | N/A | Single-byte BCD (0x00-0x99) |
| UINT8 / INT8 | 1 byte | N/A | |
| DATA1B / DATA1C | 1 byte | N/A | Signed data / complement |
| CHAR1 / HEX1 | 1 byte | N/A | |
| UINT16 / INT16 | 2 bytes | Little-endian | LSB first (eBUS standard) |
| UINT16R / INT16R | 2 bytes | Big-endian | Reversed byte order |
| DATA2B / DATA2C | 2 bytes | Little-endian | DATA2C has fixed-point scale 1:16 |
| DATA2BR / DATA2CR | 2 bytes | Big-endian | |
| CHAR2 / HEX2 | 2 bytes | | |
| CHAR3 / HEX3 | 3 bytes | | |
| UINT32 / INT32 | 4 bytes | Little-endian | |
| UINT32R / INT32R | 4 bytes | Big-endian | |
| FLOAT4 / FLOAT4R | 4 bytes | LE / BE | IEEE 754 |
| CHAR4-8 / HEX4-8 | 4-8 bytes | | |


### ebusd TypeSpec → ebus library mapping

From `ebusd-configuration/src/_templates.tsp` (base) and `vaillant/_templates.tsp` (Vaillant extensions):

#### Supported natively by ebus library

| ebusd TypeSpec | ebus library DataType | Source file | Notes |
|---|---|---|---|
| UCH | UINT8 | _templates.tsp | Unsigned char |
| SCH | INT8 | _templates.tsp | Signed char |
| D1B | DATA1B | _templates.tsp | 1-byte data |
| D1C | DATA1C | _templates.tsp | 1-byte complement |
| D2B | DATA2B | _templates.tsp | 2-byte data (unsigned) |
| D2C | DATA2C | _templates.tsp | 2-byte fixed-point (scale 1:16) |
| INT16 | INT16 | _templates.tsp | 16-bit signed |
| UINT16 | UINT16 | _templates.tsp | 16-bit unsigned |
| INT32 | INT32 | _templates.tsp | 32-bit signed |
| UINT32 | UINT32 | _templates.tsp | 32-bit unsigned |
| FLT / FLOAT | FLOAT4 | _templates.tsp | 32-bit IEEE 754 float |
| STR | CHAR1-8 | _templates.tsp | Variable-length string |
| IGN | (skip) | _templates.tsp | Ignore field |
| UIN | UINT16R | vaillant/_templates.tsp | Big-endian unsigned int (Vaillant) |
| SIN | INT16R | vaillant/_templates.tsp | Big-endian signed int |
| UIR | UINT16 | vaillant/_templates.tsp | Little-endian unsigned int (reversed of UIN) |
| ULG | UINT32R | vaillant/_templates.tsp | Big-endian unsigned long |
| BCD | BCD | _templates.tsp | Single-byte BCD |

#### NOT supported natively by ebus library — handled by Data Profiles / App layer

| ebusd TypeSpec | Description | Action |
|---|---|---|
| BCD3 / BCD4 | 3 or 4-byte BCD | Data profile with UINT32 or custom decode |
| PIN | 4-digit BCD password | Data profile (BCD3-style or 2-byte BCD) |
| HCL | Hour counter (BCD) | Data profile with custom BCD handling |
| BTI / BDA / BDY | Binary time / Date / Weekday | Data profile with value mapping |
| HDA3 / TTM / VTI / VTM | Holiday date / Timers | Data profile |
| EXP / VA | Fixed-point with divisor/offset | Data profile (e.g. `uint8_kw_div10`, `int8_percent_div10`) |
| BI0-BI2 | Bit extraction | Data profile with bit manipulation |

---

## Data Profiles (`profiles/data_profiles.json`)

The app defines **28 compile-time data profiles** that map eBUS wire formats to display units, scaling, precision, and validation ranges. These are generated into `include/data_profile_gen.hpp` at build time.

### Naming Convention

Profiles follow the pattern: `{datatype}_{unit}` with optional suffixes:
- `_div{N}` — divider ≠ 1 (e.g., `uint8_kw_div10` = divider 10)
- `_d{N}` — non-standard digit precision (e.g., `uint8_d2` = 2 decimals)

### Profile List (aligned with ebusd `@step` annotations)

| Profile | Datatype | Unit | Divider | Digits | Min | Max | ebusd scalar | @step |
|---|---|---|---|---|---|---|---|---|
| `data2c_celsius` | DATA2C | °C | 1 | 1 | -50 | 180 | `temp` / `temps` | 0.5 |
| `data2b_celsius` | DATA2B | °C | 1 | 1 | -50 | 180 | `temp2` | 0.5 |
| `data1c_celsius` | DATA1C | °C | 1 | 1 | 0 | 75 | `temp1` | — |
| `int8_celsius` | INT8 | °C | 1 | 0 | -50 | 180 | — | — |
| `uint8_celsius` | UINT8 | °C | 1 | 0 | -50 | 180 | `temp0` | — |
| `data2b_bar` | DATA2B | bar | 1000 | 1 | 0 | 70 | `press` | 0.5 |
| `float4_bar` | FLOAT4 | bar | 1 | 1 | 0 | 70 | `press` (FLT) | 0.5 |
| `int16_bar` | INT16 | bar | 1000 | 1 | 0 | 70 | `pressm` | — |
| `int16_percent` | INT16 | % | 1 | 1 | -100 | 100 | `percents` | — |
| `uint8_percent` | UINT8 | % | 1 | 0 | 0 | 100 | `percent0` | — |
| `int8_percent_div10` | INT8 | % | 10 | 1 | -100 | 100 | `percent2` | 0.5 |
| `uint32_hour` | UINT32 | h | 1 | 0 | 0 | 0 | `hoursum` | — |
| `uint16_hour` | UINT16 | h | 1 | 0 | 0 | 0 | `hoursum2` | — |
| `uint16_min_div120` | UINT16 | min | 120 | 0 | 0 | 0 | `minutes` | — |
| `data2c_lpm` | DATA2C | L/min | 1 | 0 | 0 | 0 | — | — |
| `uint16_lph` | UINT16 | l/h | 1 | 0 | 0 | 20 | `flowrate` | — |
| `uint16_lph_div100` | UINT16 | l/h | 100 | 1 | 0 | 20 | `flowrate100` | — |
| `uint16_m3h` | UINT16 | m³/h | 1 | 0 | 0 | 400 | `airflowrate` | — |
| `uint16_rpm` | UINT16 | rpm | 1 | 0 | 0 | 1000 | `fanspeed` | — |
| `uint8` | UINT8 | — | 1 | 0 | 0 | 0 | `UCH` | — |
| `uint16` | UINT16 | — | 1 | 0 | 0 | 0 | `UINT16` | — |
| `uint32` | UINT32 | — | 1 | 0 | 0 | 0 | `UINT32` | — |
| `uint32_kwh` | UINT32 | kWh | 1 | 2 | 0 | 0 | `energy` | — |
| `uint8_kw` | UINT8 | kW | 1 | 1 | 0 | 0 | `power` | — |
| `uint8_kw_div10` | UINT8 | kW | 10 | 1 | 0 | 0 | — | — |
| `uint16_kw_div10` | UINT16 | kW | 10 | 1 | 0 | 0 | — | — |
| `uint8_d2` | UINT8 | — | 1 | 2 | 0 | 0 | — | — |
| `char1` | CHAR1 | — | 1 | 0 | 0 | 0 | `CHAR1` | — |

### Digits Alignment Rule

The `digits` field controls JSON output formatting and is aligned with ebusd `@step` annotations:
- `@step(0.5)` → **1 decimal place** (0.5 resolution)
- `@step(1)` or integer types → **0 decimal places**
- Dividers (`_div10`, `_div100`) shift precision but digits reflect display resolution

### Per-Field Min/Max Override

Commands can override profile defaults per-field in `commands.json`:

```json
{
  "key": "32",
  "name": "Basement/Temperature_NightRoomSetPoint",
  "fields": [{
    "name": "value",
    "profile": "data1c_celsius",
    "position": 1,
    "min": 15,
    "max": 20
  }]
}
```

- Profile `data1c_celsius` default: min=0, max=75
- This command overrides: min=15, max=20
- Overrides are persisted to LittleFS and included in `/api/v1/commands` output

---

## Communication Modes (read, write, passive)

### Application Layer Mapping (`src/`)

The app maps command JSON fields (`config/simulation.json` / `commands.json`) to library API calls:

| Mode | Command JSON field | Library API used | Notes |
|---|---|---|---|
| Active Read (Poll) | `interval > 0`, `read_cmd` | `Controller::addPollItem()` | Registered with PollManager |
| Write | `write_cmd` (non-empty) | `Controller::enqueue()` | Triggered via MQTT `set/<key>` or HTTP POST `/api/v1/commands/write` |
| Passive / Listen | `interval: 0` | `commandManager.updateData()` | Matches incoming master telegrams passively |
| Master field | `master: true` | `command.cpp` extraction | Field extracted from master payload |
| Slave field | `master: false` | `command.cpp` extraction | Field extracted from slave payload |

### Command Struct & Compact Layout

`Command` objects are optimized for minimal memory footprint:
- `key_id_` *(uint8_t)*: Key string pool ID
- `name_id_` *(uint8_t)*: Command name string pool ID (`"Buffer/Middle_Temperature"`)
- `read_cmd_` *(PollSequence)*: Master read telegram bytes
- `write_cmd_idx_` *(uint8_t)*: Index into `CommandManager::write_cmds_` array
- `interval_` *(uint16_t)*: Polling interval in seconds (`0` = passive)
- `poll_id_` *(uint16_t)*: Active poll handle (assigned by PollManager)
- `last_` *(uint32_t)*: Last update timestamp
- `data_` *(Sequence)*: Latest raw payload bytes
- `fields_` *(StaticVector<FieldRef, 4>)*: Up to 4 fields per command

Each `FieldRef` uses only **4 bytes**:
- `name_id` *(uint8_t)*: Field name string pool ID
- `profile_idx` *(uint8_t)*: 1-based index into DataProfile registry
- `position` *(uint8_t)*: 1-based offset position within the payload (0 = none)
- `ha_profile_idx` *(uint8_t)*: 1-based index into HAProfile registry (`0` = disabled)

Min/max overrides are **not stored in `FieldRef`**. They live in a separate
`CommandManager::field_overrides_` pool (`ebus::StaticVector<FieldOverride, 16>`),
mirroring how `write_cmds_` is hoisted out of `Command`. This keeps the hot
`Command`/`FieldRef` struct at 4 bytes per field while >95 % of fields use profile
defaults directly.

`FieldOverride` (12 bytes each, ~192 bytes max for 16 entries):
- `key_id` *(uint8_t)*: Key string pool ID of the command owning the override
- `field_idx` *(uint8_t)*: 0-based index into that command's `fields_` vector
- `min_override` *(float)*: `NaN` = use profile default
- `max_override` *(float)*: `NaN` = use profile default



