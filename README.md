# esp8266-arduino-ebus

**Warning: Do not power your adapter from a power supply on eBus terminals - you will burn the transmit circuit (receive may still work)!**

## Quickstart
- connect adapter to ebus
- you should see at least one LED on the adapter shining (HW v3.0+) - if not, switch eBus wires
- LED D1 blinking indicates activity on the bus. The adapter comes pre-adjusted but if D1 is still on or off, you need to re-adjust it:
  - by a configuration in web interface for v6.0 and newer:
    - open http://esp-ebus.local/param to adjust PWM value
    - the default value is 130, max is 255, min is 1
    - when D1 is still on, you need to lower the value
    - when D1 is still off, you need to raise the value
  - using trimer RV1 for v5.x and older:
    - Note: the following directions are reversed (clockwise/counterclockwise) for adapters purchased via elecrow)
    - turn the trimmer counterclockwise until you find the position between D1 blinking and still on
    - turn the trimmer clockwise until you find the position between D1 blinking and still off
    - count the turns between these positions and set the trimmer in the middle position with D1 blinking
    - if you have adjusted the trimmer, disconnect and connect the adapter to bus again
- search for WiFi networks, you should see network with name "esp-eBus"
- connect to the network - a configuration page should open automatically
- configure your WiFi network settings (SSID, password)
- after reboot, you should be able to run `ping esp-ebus.local` successfully from a computer in your network (if your network is correctly configured for mDNS)
- to verify there are bytes being received by the adapter, you can connect to `esp-ebus.local` port `3334` using telnet - you should see unreadable binary data
- you can use [ebusd](https://github.com/john30/ebusd) to decode bus messages, see ports section for device option configuration
- the adapter listens on following TCP ports (latest SW):
  - 3333 - raw - on this port you can both read and write to the eBus - ebusd config: `-d esp-ebus.local:3333`
  - 3334 - listen only port - everything sent to this port will be discarded and not sent to bus
  - 3335 - [enhanced protocol](https://github.com/john30/ebusd/blob/b5d6a49/docs/enhanced_proto.md) - ebusd config: `-d enh:esp-ebus.local:3335`
  - 5555 - status server - you can telnet to this port (or http://esp-ebus.local:5555) to see some basic status info

## Hardware revisions

This section lists adapter hardware revisions together with specifics for each one. Each revision lists only change from the previous one.

### v1.0
- MCU: ESP8266
- step-down: MP2307
- SMD trimmer

### v2.0
- firmware file: firmware-HW_v3.x.bin
- step-down: XL7015
- RESET_PIN: MCU GPIO5
- replaced SMD to multiturn timmer
- added TX-disable - GPIO2 - function not working

### v3.0
- firmware file: firmware-HW_v3.x.bin
- step-down: ME3116
- RESET_PIN: MCU GPIO5
- fixed TX-disable - GPIO2 - blue LED on module shines when TX enabled
- added programming header
- added LEDS for TX, RX, Power
- added tp2 - you can apply 24V external power supply between tp2 (+) and BUS GND (-). If you remove D4, you can use an adapter with voltage 5-24V

### v4.0
- firmware file: firmware-HW_v4.x.bin
- RESET_PIN: TX-DISABLE (GPIO5)
- moved TX-DISABLE to GPIO5
- LEDs position changed
- added tp3, jp1 - you can apply 24V external power supply between tp2 (+) and tp3 (-). If you cut jp1, you can use any adapter with voltage 5-24V

### v4.1
- firmware file: firmware-HW_v4.x.bin
- RESET_PIN: TX-DISABLE (GPIO5)
- added debug pin to programming header

### v5.0
- firmware file: firmware-HW_v5.x.bin
- MCU changed to ESP32-C3
- RESET_PIN: TO-EBUS (GPIO20)
- removed TX-DISABLE - MCU doesn't transmit any messages during startup on UART pins
- added USB-C connector for power and/or programming
  - USB power works only with USB-A - USB-C cables (bug)
- added VCC selector jumper - you can choose from:
  - power from EBUS: jumper in position VCC-VBUS
  - power from 5V USB-C connector: jumper in position VCC-VUSB
  - power from any adapter 5-24V: remove jumper and connect adapter to VCC and GND pins
- SOD-123 diodes
- added LED D8 for MCU status

### v5.1
- firmware file: firmware-HW_v5.x.bin
- RESET_PIN: TO-EBUS (GPIO20)
- fixed reference voltage resistor value

### v5.2
- firmware file: firmware-HW_v5.x.bin
- RESET_PIN: TO-EBUS (GPIO20)
- USB power works with USB-C - USB-C cables
- replaced VCC selector jumper with 2.0mm pitch:
  - power from EBUS: jumper in position VCC-VBUS
  - power from 5V USB-C connector: jumper removed
  - power from any adapter 5-24V: remove jumper and connect adapter to VCC and GND pins

### v6.0
- firmware file: firmware-HW_v5.x.bin
- RESET_PIN: TO-EBUS (GPIO20)
- trimmer is replaced by PWM setting in web interface

### v6.1
- firmware file: firmware-HW_v5.x.bin
- RESET_PIN: TO-EBUS (GPIO20)
- added missing via in v6.0

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
- check which RESET_PIN is used in your adapter (see hardware revisions)
  - note: RESET_PIN has been changing in different software versions. The defined value refers to latest software revision. If the value doesn't work, you may try also pin ESP-RX/FROM-EBUS
- disconnect device from bus
- connect RESET_PIN and GND pins using a wire
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
- check for correct firmware file (see hardware revisions)
- inside the project folder run:
  - for firmware-HW_v3.x.bin: `pio run -e esp12e-v3.0-ota -t upload`
  - for firmware-HW_v4.x.bin: `pio run -e esp12e-ota -t upload`
  - for firmware-HW_v5.x.bin: `pio run -e esp32-c3-ota -t upload`

### espota.py
- you need python installed in your computer
- download [espota.py script](https://github.com/esp8266/Arduino/blob/master/tools/espota.py)
  - for Windows, you can download espota.exe from [esp32-xx.zip](https://github.com/espressif/arduino-esp32/releases) - it is located in `tools` folder
- download firmware according to your hardware version from https://github.com/danielkucera/esp8266-arduino-ebus/releases
- use port number:
  - 8266 - for esp8266 (HW up to v4.1)
  - 3232 - for esp32-c3 (HW from v5.0 up)
- to upgrade, run:
```
$ python3 espota.py -i esp-ebus.local -f <FIRMWARE_FILE_NAME> -p <PORT_NUMBER> -r -d
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
- if that doesn't work, connect also TP1 to 3V3 and try again (see Issue #27)


