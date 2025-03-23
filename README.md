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
- connect to the network - a configuration page should open automatically, default password is: ebusebus
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

### v6.1 and newer
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
- physically using a USB-TTL adaptor or device USB port (HW v5.0+)

### web interface
- open web interface of the device by IP or on: http://esp-ebus.local
- find the update link
- upload correct firmware file (see hardware revisions) - use version WITHOUT `fullflash` keyword
- click `Update` button
- wait for restart, reconnect to adapter and configure WiFi if not connected automatically
- in case you cannot open web interface, [reset device](#config-reset) to access it

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

### upgrading over USB (HW v5.0+)
 - this version has built-in USB serial interface
 - download `firmware-fullflash-*` firmware from https://github.com/danielkucera/esp8266-arduino-ebus/releases
 - connect `PROG` and `GND`
 - connect adapter to a PC using USB-A - USB-C cable
 - you should see a new serial port
 - flash the firmware to address 0x0 using either one of tools:
   - Web based: https://adafruit.github.io/Adafruit_WebSerial_ESPTool/
   - Windows: using Flash Download Tools from https://www.espressif.com/en/support/download/other-tools
   - Linux esptool.py: `esptool.py write_flash 0x0 firmware-fullflash-*`


### upgrading over USB-TTL adaptor (before HW v5.0)
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


## MQTT support
**Warning: The current implementation does not support TLS encryption. The MQTT server should therefore be run in a secure environment!**


### MQTT configuration
At the configuration web page you can set
- server IP address or hostname
- user name
- password


### MQTT interface
- Basic device data, settings and various counters are published regularly.
 
The device's **root topic** begins with ebus/, followed by the last 6 characters of its MAC address as a unique device ID and a trailing slash.
For e.g. `ebus/8406AC/`

The following subtopics are available on every device.
|***subtopic***                 |***description***
|:-                             |:-
|**output**                     |
|device                         |information about your device                                   
|device/ebus                    |basic ebus adapter settings (configration)
|device/firmware                |details of installed firmware
|device/wifi                    |wifi details
|&nbsp;                         |&nbsp;
|**counter**                    | 
|state/arbitration              |arbitration over common interface (e.g. ebusd)
|&nbsp;                         |&nbsp;
|**input**                      |
|cmd/restart                    |restarting of the device 


### MQTT interface with firmware EBUS_INTERNAL=1
- `EBUS_INTERNAL=1` adds a scheduler and an eBUS command buffer to the device.

The following subtopics are available.
|***subtopic***                 |***description***
|:-                             |:-
|**output**                     |
|commands                       |installed commands
|values                         |received values of installed commands
|sent                           |result of send command: subtopic=master; value=slave
|raw                            |values of raw data printout: subtopic=master; value=slave
|&nbsp;                         |&nbsp;
|**counter**                    |
|state/internal/messages        |processed messages
|state/internal/errors          |errors of finite state machine  
|state/internal/resets          |resets of finite state machine 
|state/internal/requets         |bus requests (arbitration)
|&nbsp;                         |&nbsp;
|**input**                      |
|cmd/insert                     |inserting (installing) a new command
|cmd/remove                     |removing an installed command
|cmd/list                       |list all installed commands
|cmd/load                       |loading (install) of saved commands
|cmd/save                       |saving of current installed commands
|cmd/wipe                       |wiping of saved commands
|cmd/send                       |sending of given ebus command(s) once
|cmd/raw                        |toggling of raw data printout
|cmd/filter                     |adding filter(s) for raw data printout
|&nbsp;                         |&nbsp;
|**response of input**          |
|cmd/loading                    |bytes of loaded commands
|cmd/saving                     |bytes of saved commands
|cmd/wiping                     |bytes of wiped commands
|cmd/error                      |message of last occurred error


### Details of MQTT commands
The provided examples were created using `Mosquitto` in a `Linux shell`. The `server IP address or hostname` and `unique device ID` must be adjusted to your environment.

- `server IP address or hostname = server`
- `unique device ID = 8406AC`
- `root topic = ebus/8406AC/`

**Restarting of the device**
```
* subtopic: cmd/restart
* payload:  true
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/restart' -m 'true' 
```

**Inserting (Installing) a new command**
```
* subtopic: cmd/insert
* payload:  ebus command in form of "ZZPBSBNNDBx" with a UNIQUE_KEY for e.g.
{
  "key": "UNIQUE_KEY",               // unique key of command
  "command": "fe070009",             // ebus command as vector of "ZZPBSBNNDBx"
  "unit": "°C",                      // unit of the received data
  "active": false,                   // active sending of command
  "interval": 0,                     // minimum interval between two commands in seconds
  "master": true,                    // true..master false..slave
  "position": 1,                     // starting byte in payload (DBx)
  "datatype": "DATA2b",              // ebus datatype
  "topic": "outdoor/temperature",    // mqtt subtopic below "values/"
  "ha": true,                        // home assistant support for auto discovery
  "ha_class": "temperature"          // home assistant device_class
}

ebus datatype: BCD, UINT8, INT8, UINT16, INT16, UINT32, INT32, DATA1b, DATA1c, DATA2b, DATA2c, FLOAT (values as 1/1000)
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/insert' -m '{"key":"01","command":"fe070009","unit":"°C","active":false,"interval":0,"master":true,"position":1,"datatype":"DATA2b","topic":"outdoor/temperature","ha":true,"ha_class":"temperature"}'
```

**Removing an installed command**
```
* subtopic: cmd/remove
* payload:  UNIQUE_KEY of ebus command
{
  "key": "UNIQUE_KEY"
}
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/remove' -m '{"key":"01"}'
```

**List all installed commands**
```
* subtopic: cmd/list
* payload:  true
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/list' -m 'true'
```

**Loading (install) of saved commands**
```
* subtopic: cmd/load
* payload:  true
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/load' -m 'true'
```

**Saving of current installed commands**
```
* subtopic: cmd/save
* payload:  true
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/save' -m 'true'
```

**Wiping of saved commands**
```
* subtopic: cmd/wipe
* payload:  true
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/wipe' -m 'true'
```

**Sending of given ebus command(s) once**
```
* subtopic: cmd/send
* payload:  array of ebus command(s) in form of "ZZPBSBNNDBx" for e.g.
[
  "05070400",
  "15070400"
]
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/send' -m '["05070400","15070400"]'
```

**Toggling of the raw data printout**
```
* subtopic: cmd/raw
* payload:  true | false
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/raw' -m 'true'
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/raw' -m 'false'
```

**Adding filter(s) for raw data printout**
```
* subtopic: cmd/filter
* payload:  array of sequences for e.g.
[
  "0700",
  "fe"
]
```
```
mosquitto_pub -h server -t 'ebus/8406AC/cmd/filter' -m '["0700","fe"]'
```

