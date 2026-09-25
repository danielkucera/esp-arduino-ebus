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

## Bridge UART timing

The network bridge drains UART RX in a dedicated high-priority task, independently
of TCP clients. Arbitration uses a reconstructed symbol start timestamp rather
than a delayed network-loop timestamp. Batched or overflowing RX data is not
used as evidence of a valid arbitration deadline. The byte values remain in the
forwarding queue; a full queue increments the error counter and resets arbitration.

Console output uses USB Serial/JTAG, not UART0 (GPIO21 is the eBUS RX pin).
Application-only OTA cannot change an older bootloader's console configuration;
the application releases that pin before initializing the bus. No partition
layout change is included. These fixes do not claim to eliminate every physical
bus fault; occasional self-recovering eBUS warnings remain under investigation.
