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
- connect to esp-ebus.local TCP port 3333 using telnet to verify there are bytes being received by the adapter
- if you are using [ebusd](https://github.com/john30/ebusd), you can configure it use adapter by following parameters: `-d esp-ebus.local:3333`
- if you are going to transmit to ebus, I also recommend to increase latency limit to, e.g.: `--latency=200000`

## Upgrading
There are two options:
- using platform.io
  - heavier option - it will compile the firmware from source code and upload using internall tooling
- using espota.py
  - lightweight - just needs OTA script and precompiled firmware file

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
- to upgrade, run:
```
$ python3 espota.py -i esp-ebus.local -f firmware.bin -r
Uploading: [============================================================] 100% Done...
```



