# esp-arduino-ebus

ESP-based Wi-Fi firmware for eBUS adapter hardware

**Warning: Do not power your adapter from a power supply on eBus terminals - you will burn the transmit circuit (receive may still work)!**

To get more info navigate to [wiki](https://github.com/danielkucera/esp-arduino-ebus/wiki)

`esp-arduino-ebus` is an open-source firmware for [EBUS to WiFi Adapter Module](https://danman.eu/ebus-adapter).  
It turns the adapter into a **network-connected eBUS interface** with TCP, MQTT, HTTP, and Home Assistant support — suitable for monitoring and controlling eBUS-based heating systems.

> ⚠️ **This firmware is designed to run only on supported eBUS adapter boards.**  
> It is **not** intended for bare ESP modules without the required eBUS interface circuitry.

---

## What This Project Does

- 🔌 Connects to **eBUS heating systems** (Vaillant and other eBUS-compatible HVAC equipment)
- 📡 Connects the physical eBUS line to a network over **Wi-Fi**
- 🌍 Exposes eBUS traffic over **TCP sockets** compatible with tools like `ebusd`
- 📊 Publishes data to **MQTT** for smart home integration
- 🏠 Supports **Home Assistant autodiscovery**
- ⚙️ Provides a **web interface** for configuration and diagnostics

### Two firmware variants

- **Bridge** (`esp32-c3`, default) — transparent bus interface for external
  software like `ebusd` on another host. No autonomous participation.
- **INTERNAL** (`esp32-c3-internal`) — autonomous participant: polls,
  commands, MQTT/HA on-device, no external software needed.

Details: wiki [5. Bridge mode](https://github.com/danielkucera/esp-arduino-ebus/wiki/5.-Bridge-mode) and
[6. INTERNAL firmware](https://github.com/danielkucera/esp-arduino-ebus/wiki/6.-INTERNAL-firmware-current).

---

## Build and Flash

The project uses PlatformIO with the ESP-IDF framework. Install PlatformIO,
clone this repository, and run commands from the repository root.

Build the default network bridge firmware:

```bash
pio run -e esp32-c3
```

Build the standalone INTERNAL firmware instead:

```bash
pio run -e esp32-c3-internal
```

To flash over a connected serial port, use the matching environment and set
`upload_port` in `platformio.ini` if PlatformIO does not detect the port:

```bash
pio run -e esp32-c3 --target upload
```

INTERNAL firmware can also be uploaded over OTA to `esp-ebus.local`:

```bash
pio run -e esp32-c3-internal-ota --target upload
```

The additional `esp32-c3-internal-simulation` environment enables virtual bus
simulation. For command and HTTP API examples, see
[`doc/commands.md`](doc/commands.md).

---

## Smart Home Integration

- Native **MQTT support**
- **Home Assistant autodiscovery**
- Seamless integration into existing automation setups
- Suitable for dashboards, logging, and energy optimization

---

## Required Hardware

This firmware **requires a compatible eBUS adapter board**, which provides:

- Proper **eBUS level shifting and electrical protection**
- Safe **bus power handling**
- Signal conditioning (PWM / comparator circuitry)
- Reliable physical connection to the eBUS line

Supported hardware revisions include multiple ESP32-based eBUS adapter boards maintained alongside this project.

> ❌ Flashing this firmware onto a generic ESP8266/ESP32 module **will not work**.

---

## Why Choose esp-arduino-ebus?

- ✅ Designed for **real eBUS adapter hardware**
- 🔓 Fully **open source**
- ⚡ Low-power, always-on operation
- 🔧 Flexible: bridge mode or INTERNAL standalone mode
- 🧩 Compatible with existing eBUS tools and ecosystems

---