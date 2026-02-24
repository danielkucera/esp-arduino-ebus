#esptool.py erase_flash
pio run -e esp32-c3 -t upload

#/usr/bin/python3 /home/danman/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 460800 --before default_reset --after hard_reset write_flash -z --flash_mode dio --flash_freq 80m --flash_size 4MB 0x0000 /storage/Projects/esp8266-arduino-ebus/.pio/build/esp32-c3/bootloader.bin 0x8000 /storage/Projects/esp8266-arduino-ebus/.pio/build/esp32-c3/partitions.bin 0xe000 /home/danman/.platformio/packages/framework-arduinoespressif32@3.20006.221224/tools/partitions/boot_app0.bin 0x10000 .pio/build/esp32-c3/firmware.bin
