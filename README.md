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
- 📡 Bridges the physical eBUS line to **Wi-Fi / Ethernet**
- 🌍 Exposes eBUS traffic over **TCP sockets** compatible with tools like `ebusd`
- 📊 Publishes data to **MQTT** for smart home integration
- 🏠 Supports **Home Assistant autodiscovery**
- ⚙️ Provides a **web interface** for configuration and diagnostics

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

## 🧠 INTERNAL Firmware Mode

In addition to acting as a network bridge, `esp-arduino-ebus` offers an advanced **INTERNAL firmware mode**, allowing the adapter to behave as an **active, autonomous eBUS participant**.

### Key INTERNAL Features

- 🧾 **Internal command store** for eBUS messages
- 💾 **Persistent storage** in flash, restored after reboot
- 🔍 **Automatic eBUS device scanning**
- 🔄 **Active and passive operation** on the bus
- 📡 **Full remote control via MQTT or HTTP**

### INTERNAL Control Capabilities

- Insert or remove stored eBUS commands
- Send single or periodic commands
- Publish stored values on demand
- Enable filtering and forwarding rules
- Trigger bus scans and internal resets

This mode enables **standalone operation** without requiring external software such as `ebusd`.

---

## Smart Home Integration

- Native **MQTT support**
- **Home Assistant autodiscovery**
- Seamless integration into existing automation setups
- Suitable for dashboards, logging, and energy optimization

---

## Why Choose esp-arduino-ebus?

- ✅ Designed for **real eBUS adapter hardware**
- 🔓 Fully **open source**
- ⚡ Low-power, always-on operation
- 🔧 Flexible: bridge mode or INTERNAL standalone mode
- 🧩 Compatible with existing eBUS tools and ecosystems

---

## Application-only upgrades from IotWebConf firmware

On first boot, legacy device name, admin password, Wi-Fi credentials and PWM
are imported before Wi-Fi starts. Existing current-format values take precedence.
Legacy data remains intact for recovery; a separate migration marker prevents
reimport after a deliberate configuration reset. Invalid or truncated records
are rejected, and the marker is written only after successful migration.

Upload a regular application image, not a full-flash image. Check the installed
OTA slot size first: legacy layouts may have only 0x140000 bytes per application.
The bridge build is size-optimized and CI checks that limit. The larger internal
variant must not be assumed to fit the same layout. Application-only OTA leaves
the existing bootloader and partition table in place and does not enable new
bootloader rollback capabilities.
