# Device Commands

`<device-ip>` is a placeholder for the adapter address (e.g. `192.168.1.9`).
All examples target the `esp32-c3-internal` build.

## Firmware update

```bash
# OTA update via espota
python3 tools/espota.py \
  -i <device-ip> \
  -p 3232 \
  -f .pio/build/esp32-c3-internal/firmware.bin \
  --debug \
  --progress

# Firmware upload via HTTP
curl -X POST \
  -H "Content-Type: application/octet-stream" \
  --data-binary @.pio/build/esp32-c3-internal/firmware.bin \
  http://<device-ip>/api/v1/upgrade/upload
```

## Command set upload

```bash
# Command set upload (commands.json below is just an example — use your own)
curl -X POST \
  -H "Content-Type: application/json" \
  --data-binary @commands.json \
  http://<device-ip>/api/v1/app/commands/upload
```

## Health + diagnostics (GET)

```bash
curl http://<device-ip>/api/v1/health
curl http://<device-ip>/api/v1/metrics
curl http://<device-ip>/api/v1/system/heap
curl http://<device-ip>/api/v1/system/tasks
curl http://<device-ip>/api/v1/devices
curl http://<device-ip>/api/v1/app/values
curl http://<device-ip>/api/v1/app/logs
```

## Force a poll (POST, JSON)

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"key":"01"}' \
  http://<device-ip>/api/v1/app/values/read
```

## Write a value (POST, JSON)

Payload shape depends on the command — verify against one writable
command before use. Schematic example:

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -d '{"key":"42","value":55}' \
  http://<device-ip>/api/v1/app/values/write
```

## Bus device scan (POST, empty body)

```bash
curl -X POST \
  http://<device-ip>/api/v1/devices/scan
```

## Breaker + metrics reset (POST, empty body)

```bash
curl -X POST \
  http://<device-ip>/api/v1/metrics/breaker/reset

curl -X POST \
  http://<device-ip>/api/v1/metrics/reset
```

## WiFi scan (POST, empty body)

```bash
curl -X POST \
  http://<device-ip>/api/v1/network/wifi/scan
```

## Restart (POST, empty body)

```bash
curl -X POST \
  http://<device-ip>/restart
```
