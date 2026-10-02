# eBUS DataType and App Data Profile Mapping

This document describes how eBUS wire types map to the library's native
`ebus::DataType` values and how the firmware's data profiles add display and
validation metadata. The profile JSON files are the source of truth for the
profile inventory; see [`profiles/README.md`](../profiles/README.md) for profile
authoring and user overlays.

## ebus library native DataTypes

Defined in [`lib/ebus/include/ebus/data_types.hpp`](../lib/ebus/include/ebus/data_types.hpp).
Multi-byte numeric types use little-endian byte order by default; types ending
in `R` reverse that byte order. `DATA2B` and `DATA2C` are signed fixed-point
types with factors of 1/256 and 1/16, respectively.

| ebus library DataType | Size | Endianness | Notes |
|---|---|---|---|
| BCD | 1 byte | N/A | Packed decimal |
| UINT8 / INT8 | 1 byte | N/A | Unsigned / signed integer |
| DATA1B / DATA1C | 1 byte | N/A | eBUS 1-byte data encodings |
| CHAR1 / HEX1 | 1 byte | N/A | Character / hexadecimal data |
| UINT16 / INT16 | 2 bytes | Little-endian | |
| UINT16R / INT16R | 2 bytes | Big-endian | Reversed byte order |
| DATA2B / DATA2C | 2 bytes | Little-endian | Signed fixed-point: factors 1/256 / 1/16 |
| DATA2BR / DATA2CR | 2 bytes | Big-endian | Reversed-byte-order DATA2B / DATA2C |
| CHAR2 / HEX2 | 2 bytes | N/A | Character / hexadecimal data |
| CHAR3 / HEX3 | 3 bytes | N/A | Character / hexadecimal data |
| UINT32 / INT32 | 4 bytes | Little-endian | |
| UINT32R / INT32R | 4 bytes | Big-endian | Reversed byte order |
| FLOAT4 / FLOAT4R | 4 bytes | Little-endian / big-endian | IEEE 754 |
| CHAR4-8 / HEX4-8 | 4-8 bytes | N/A | Character / hexadecimal data |


### ebusd TypeSpec → ebus library mapping

Representative mappings from ebusd's base `_templates.tsp` and Vaillant
`vaillant/_templates.tsp` definitions:

#### Supported natively by ebus library

| ebusd TypeSpec | ebus library DataType | Source file | Notes |
|---|---|---|---|
| UCH | UINT8 | _templates.tsp | Unsigned char |
| SCH | INT8 | _templates.tsp | Signed char |
| D1B | DATA1B | _templates.tsp | 1-byte data |
| D1C | DATA1C | _templates.tsp | 1-byte complement |
| D2B | DATA2B | _templates.tsp | 2-byte signed fixed-point (factor 1/256) |
| D2C | DATA2C | _templates.tsp | 2-byte fixed-point (scale 1:16) |
| INT16 | INT16 | _templates.tsp | 16-bit signed |
| UINT16 | UINT16 | _templates.tsp | 16-bit unsigned |
| INT32 | INT32 | _templates.tsp | 32-bit signed |
| UINT32 | UINT32 | _templates.tsp | 32-bit unsigned |
| FLT / FLOAT | FLOAT4 | _templates.tsp | 32-bit IEEE 754 float |
| STR | CHAR1-8 | _templates.tsp | String data, represented by a fixed-size CHAR type |
| IGN | (skip) | _templates.tsp | Ignore field |
| UIN | UINT16R | vaillant/_templates.tsp | Big-endian unsigned int (Vaillant) |
| SIN | INT16R | vaillant/_templates.tsp | Big-endian signed int |
| UIR | UINT16 | vaillant/_templates.tsp | Little-endian unsigned int (reversed of UIN) |
| ULG | UINT32R | vaillant/_templates.tsp | Big-endian unsigned long |
| BCD | BCD | _templates.tsp | Single-byte BCD |

#### No direct native DataType equivalent

| ebusd TypeSpec | Description | Action |
|---|---|---|
| BCD3 / BCD4 | 3- or 4-byte BCD | Requires explicit decoding; a profile alone does not provide multi-byte BCD decoding |
| PIN | BCD password representation | Requires protocol-specific decoding |
| HCL | Hour counter (BCD) | Requires explicit BCD conversion |
| BTI / BDA / BDY | Binary time / date / weekday | Requires protocol-specific field decoding |
| HDA3 / TTM / VTI / VTM | Holiday date / timer encodings | Requires protocol-specific field decoding |
| EXP / VA | Scaled values with divisor / offset semantics | Use a profile for supported underlying types and scaling; implement any additional offset or encoding behavior explicitly |
| BI0-BI2 | Bit extraction | Requires explicit bit extraction; profiles do not define bit fields |

---

## App Data Profiles (`profiles/data_profiles.json`)

The base file currently defines **43 data profiles**. At build time,
`scripts/generate_data_profiles.py` merges the optional
`profiles/data_profiles_user.json` overlay and generates
`include/app/data_profile_gen.hpp`. A user overlay can add profiles or replace
base entries with the same name, so the final compiled count can differ from
43.

### Naming Convention

Most profile names follow `{datatype}_{unit}` with optional suffixes:
- `_div{N}` — divider ≠ 1 (e.g., `uint8_kw_div10` = divider 10)
- `_d{N}` — non-standard digit precision (e.g., `uint8_d2` = 2 decimals)
Type-only profiles (such as `uint8`, `char1`, and `hex1`) are also used.

### Base profile inventory

Values below are from `profiles/data_profiles.json`. The `ebusd scalar` and
`@step` columns show known upstream associations; a dash means none is listed
here, not that the profile is unsupported by ebusd.

| Profile | Datatype | Unit | Divider | Digits | Min | Max | ebusd scalar | @step |
|---|---|---|---|---|---|---|---|---|
| `data2c_celsius` | DATA2C | °C | 1 | 1 | -50 | 180 | `temp` / `temps` | 0.5 |
| `data2b_celsius` | DATA2B | °C | 1 | 1 | -50 | 180 | `temp2` | 0.5 |
| `data1c_celsius` | DATA1C | °C | 1 | 1 | 0 | 75 | `temp1` | — |
| `int8_celsius` | INT8 | °C | 1 | 0 | -50 | 180 | — | — |
| `uint8_celsius` | UINT8 | °C | 1 | 0 | -50 | 180 | `temp0` | — |
| `data2b_bar` | DATA2B | bar | 1000 | 1 | 0 | 70 | base `press` (D2B) | 0.5 |
> Scope: base `press` only (`scalar press extends D2B`, ÷256 native).
> Vaillant `press` is `FLT`/int16 (÷1000) — using this profile there
> renders `raw/256000` (verified live: 0 for 2.4 bar); use `int16_bar`.
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
| `uint16_m3h` | UINT16 | m3/h | 1 | 0 | 0 | 400 | `airflowrate` | — |
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
| `char2` | CHAR2 | — | 1 | 0 | 0 | 0 | — | — |
| `char3` | CHAR3 | — | 1 | 0 | 0 | 0 | — | — |
| `char4` | CHAR4 | — | 1 | 0 | 0 | 0 | — | — |
| `char5` | CHAR5 | — | 1 | 0 | 0 | 0 | — | — |
| `char6` | CHAR6 | — | 1 | 0 | 0 | 0 | — | — |
| `char7` | CHAR7 | — | 1 | 0 | 0 | 0 | — | — |
| `char8` | CHAR8 | — | 1 | 0 | 0 | 0 | — | — |
| `hex1` | HEX1 | — | 1 | 0 | 0 | 0 | — | — |
| `hex2` | HEX2 | — | 1 | 0 | 0 | 0 | — | — |
| `hex3` | HEX3 | — | 1 | 0 | 0 | 0 | — | — |
| `hex4` | HEX4 | — | 1 | 0 | 0 | 0 | — | — |
| `hex5` | HEX5 | — | 1 | 0 | 0 | 0 | — | — |
| `hex6` | HEX6 | — | 1 | 0 | 0 | 0 | — | — |
| `hex7` | HEX7 | — | 1 | 0 | 0 | 0 | — | — |
| `hex8` | HEX8 | — | 1 | 0 | 0 | 0 | — | — |

### Scaling and formatting

The profile's `divider` scales a decoded numeric value as `decoded / divider`;
`digits` controls formatted decimal precision. Upstream `@step` metadata can
inform a profile's precision, but there is no universal one-to-one rule: the
profile explicitly sets `digits` and `divider`.

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
- Overrides are persisted to LittleFS and included in `/api/v1/app/commands` output

---

Command communication modes and HTTP examples are documented in
[`doc/commands.md`](commands.md). This document focuses on wire datatypes and
the display/validation metadata attached to command fields.
