# esp8266-arduino-ebus

**Warning: Do not power your adapter from a power supply on eBus terminals - you will burn the transmit circuit (receive may still work)!**

## Quickstart
- connect adapter to ebus
- you should see at least one LED on the adapter shining (v3.0+) - if not, switch eBus wires
- search for WiFi networks, you should see network with name "esp-eBus"
- connect to the network - a configuration page should open automatically
- configure your WiFi network settings (SSID, password)
- after reboot, you should be able to run `ping esp-ebus.local` successfully from a computer in your network (if your network is correctly configured for mDNS)
- LED D1 blinking indicates activity on the bus. The adapter comes pre-adjusted but if D1 is still on or off, you need to re-adjust it using trimer RV1:
  - turn the trimmer counterclockwise until you find the position between D1 blinking and still on
  - turn the trimmer clockwise until you find the position between D1 blinking and still off
  - count the turns between these positions and set the trimmer in the middle position with D1 blinking
- the adapter listens on following TCP ports (from HW rev v3.0):
  - 3333 - on this port you can both read and write to the eBus - ebusd config: `-d esp-ebus.local:3333`
  - 3334 - listen only port - everything sent to this port will be ignored and the adapters TX pin is physically isolated (TX-DISABLE)
  - 3335 - port with enhanced protocol, ebusd config: `-d enh:esp-ebus.local:3335`
  - 5555 - you can telnet to this port to see some basic status info
- to verify there are bytes being received by the adapter connect to `esp-ebus.local` port `3334` using telnet - you should see unreadable binary data
- you can use [ebusd](https://github.com/john30/ebusd) to decode bus messages, see ports section for device option configuration

## Hardware revisions

This section lists adapter hardware revisions together with specifics for each one. Each revision lists only change from the previous one.

### v1.0
- MCU: ESP8266
- step-down: MP2307
- SMD trimmer

### v2.0
- firmware file: firmware-HW_v3.x.bin 
- step-down: XL7015
- multiturn timmer

### v3.0
- firmware file: firmware-HW_v3.x.bin 
- step-down: ME3116
- added programming header
- added TX-disable function on pin 3 - blue LED on module shines when TX enabled
- added LEDS for TX, RX, Power
- added tp2 - you can apply 24V external power supply between tp2 (+) and BUS GND (-). If you remove D4, you can use an adapter with voltage 5-24V

### v4.0
- firmware file: firmware-HW_v4.x.bin 
- TX-disable moved to pin 5
- LEDs position changed
- added tp3, jp1 - you can apply 24V external power supply between tp2 (+) and tp3 (-). If you cut jp1, you can use any adapter with voltage 5-24V

### v4.1
- firmware file: firmware-HW_v4.x.bin 
- added debug pin to programming header

### v5.0
- firmware file: firmware-HW_v5.x.bin 
- MCU changed to ESP32-C3
- removed TX-DISABLE - MCU doesn't transmit any messages during startup on UART pins
- added USB-C connector for power and/or programming
- added VCC selector jumper - you can choose from:
  - power from EBUS: jumper in position VCC-VBUS
  - power from standard 5V USB-C adapter: jumper in position VCC-VUSB
  - power from any adapter 5-24V: remove jumper and connect adapter to VCC and GND pins
- SOD-123 diodes
- added LED D8 for MCU status

### v5.1
- firmware file: firmware-HW_v5.x.bin 
- fixed reference voltage resistor value

## Troubleshooting
#### The adapter seems dead, no LED shines or blinks.
The ebus wires are reversed, try switching the wires. Don't worry, it has a protection against reversing.

#### The ebus logs shows `[bus error] device status: unexpected enhanced byte 2`
Please read quickstart section to find correct device configuration options

#### I often see messages like: `ERR: arbitration lost`, `signal lost`, `signal acquired`
It's possible that the adapter has a poor WiFi reception so it has to resend messages and the latency increases. Try to improve the signal by moving the adapter closer to the WiFi AP. You can also try to increase the ebusd latency parameter to e.g. `--latency=200000`.

#### The adapter is loosing connectivity, breaking other ebus components communication, D7 is blinking, WiFi is disappearing after connecting, devices on the bus show error status or other intermittent failures.
It's possible that ebus doesn't have enough power to supply the adapter together with all other connected devices. From version v3.0 there are options for supplying external power, see the hardware revisions section for details.

#### Nothing helps. I need support.
Run ebusd with `--lograwdata=data --latency=2000 --log=all:debug` options. Then save the log, open an issue here, describe the problem and attach the log. I'll try to help you.

## Config reset
- disconnect device from bus
- connect TX-DISABLE (previously ESP-RX) and GND pins using a wire
- connect device to bus
- wait 5 seconds
- disconnect the wire

## Upgrading
There are following options:
- over the network (OTA)
  - [using web interface](#web-interface)
    - easiest
  - [using platform.io](#platformio)
    - heavier option - it will compile the firmware from source code and upload using internall tooling
  - [using espota.py](#espotapy)
    - lightweight - just needs OTA script and precompiled firmware file
- physically using a USB-TTL adaptor

### web interface
- [reset device](#config-reset) to access config portal
- connect to the esp-eBus WiFi network
- click blue `Update` button
- upload correct firmware file (see hardware revisions)
- click red `Update` button
- wait for restart, reconnect to adapter and configure WiFi

### platform.io
- clone this repository using git
- `pip3 install platformio`
- inside the project folder run:
```
pio run -e esp12e-ota -t upload
```

### espota.py
- you need python installed in your computer
- download [espota.py script](https://github.com/esp8266/Arduino/blob/master/tools/espota.py)
  - for Windows, you can download espota.exe from [esp32-xx.zip](https://github.com/espressif/arduino-esp32/releases) - it is located in `tools` folder
- download firmware according to your hardware version from https://github.com/danielkucera/esp8266-arduino-ebus/releases
- to upgrade, run:
```
$ python3 espota.py -i esp-ebus.local -f firmware.bin -r -d
16:33:23 [DEBUG]: Options: {'esp_ip': 'esp-ebus.local', 'host_ip': '0.0.0.0', 'esp_port': 8266, 'host_port': 47056, 'auth': '', 'image': 'firmware.bin', 'spiffs': False, 'debug': True, 'progress': True}
16:33:23 [INFO]: Starting on 0.0.0.0:47056
16:33:23 [INFO]: Upload size: 380320
16:33:23 [INFO]: Sending invitation to: esp-ebus.local
16:33:23 [INFO]: Waiting for device...
Uploading: [============================================================] 100% Done...

16:33:30 [INFO]: Waiting for result...
16:33:31 [INFO]: Result: OK
```

### upgrading using USB-TTL adaptor
You will need an USB-TTL adaptor (dongle) which suports 3V3 voltage levels and has 3V3 voltage output pin
- download firmware bin file from https://github.com/danielkucera/esp8266-arduino-ebus/releases
- download NodeMCU PyFlasher from https://github.com/marcelstoer/nodemcu-pyflasher/releases
- using a wire connect pins `PROG` and `TP3`
- connect your adaptor in a following way (dongle - module):
  - 3V3 <-> 3V3
  - TX  <-> ESP-RX
  - RX  <-> ESP-TX
  - GND <-> GND
- now connect the dongle to your PC - you should see two red LEDs on, blue should flash briefly and stay off (v4.0+)
- open NodeMCU PyFlasher and select your firmware file and serial port
- click Flash NodeMCU and watch the progress in Console


