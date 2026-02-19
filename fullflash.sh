#!/bin/sh

set -eu

ENV_NAME="${1:-esp32-c3}"
OF="${2:-firmware-fullflash-HW_v5.x.bin}"

tr '\0' '\377' < /dev/zero | dd bs=1 count=$((0x10000)) of="$OF"
dd if=".pio/build/${ENV_NAME}/bootloader.bin" of="$OF" conv=notrunc
dd if=".pio/build/${ENV_NAME}/partitions.bin" of="$OF" bs=1 seek=$((0x8000)) conv=notrunc
dd if="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" of="$OF" bs=1 seek=$((0xe000)) conv=notrunc
dd if=".pio/build/${ENV_NAME}/firmware.bin" of="$OF" bs=1 seek=$((0x10000))
