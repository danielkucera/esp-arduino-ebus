# esp8266-arduino-ebus

## Quickstart
- connect adapter to ebus
- search for WiFi networks, you should see network with name "esp-eBus"
- connect to the network - a configuration page should open automatically
- configure your WiFi network settings (SSID, password)
- after reboot, you should be able to run `ping esp-ebus.local` successfully from a computer in your network (if your network is correctly configured for mDNS)
- LED D1 blinking indicates activity on the bus. If it is still on or off, you need to adjust ebus level using trimer RV1:
  - turn the trimmer counterclockwise until you find the position between D1 blinking and still on
  - turn the trimmer clockwise until you find the position between D1 blinking and still off
  - count the turns between these positions and set the trimmer in the middle position with D1 blinking
- the adapter listens on two TCP ports (from HW rev v3.0):
  - 3333 - on this port you can both read and write to the eBus - the blue led will shine when TX pin is enabled
  - 3334 - listen only port - everything sent to this port will be ignored and the adapters TX pin is physically isolated (TX-DISABLE)
- to verify there are bytes being received by the adapter connect to esp-ebus.local TCP port using telnet
- if you are using [ebusd](https://github.com/john30/ebusd), you can configure it use adapter by following parameters: `-d esp-ebus.local:3333`
- if you are going to transmit to ebus, I also recommend to increase latency limit to, e.g.: `--latency=200000`

## Config reset
- disconnect device from bus
- short ESP-RX and GND pins using a wire
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
- upload `firmware.bin` file
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
- download firmware(-v3.0).bin from https://github.com/danielkucera/esp8266-arduino-ebus/releases
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


