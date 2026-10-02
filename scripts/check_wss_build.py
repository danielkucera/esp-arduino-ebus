#!/usr/bin/env python3
"""Verify the generated SDK config and OTA size; NEVER flash any device."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import subprocess

REQUIRED = (
    "CONFIG_IDF_TARGET_ESP32C3",
    "CONFIG_MQTT_TRANSPORT_SSL",
    "CONFIG_MQTT_TRANSPORT_WEBSOCKET",
    "CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE",
    "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE",
    "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL",
    "CONFIG_MBEDTLS_HAVE_TIME",
    "CONFIG_MBEDTLS_HAVE_TIME_DATE",
    "CONFIG_MBEDTLS_DYNAMIC_BUFFER",
)
FORBIDDEN = (
    "CONFIG_ESP_TLS_INSECURE",
    "CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY",
    "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEPRECATED_LIST",
    "CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA",
)


def config_defines(header: str) -> dict[str, str]:
    return dict(re.findall(r"^#define\s+(CONFIG_\w+)\s+([^\r\n]+)", header, re.MULTILINE))


def number(value: str) -> int:
    value = value.strip()
    if value.upper().endswith("K"):
        return int(value[:-1], 0) * 1024
    if value.upper().endswith("M"):
        return int(value[:-1], 0) * 1024 * 1024
    return int(value, 0)


def validate(root: Path) -> dict:
    build = root / ".pio/build/esp32-c3-internal"
    header_path = build / "config/sdkconfig.h"
    if not header_path.is_file():
        raise ValueError(f"Generated SDK configuration is missing: {header_path}")
    settings = config_defines(header_path.read_text(encoding="utf-8"))
    errors = [f"{key} must be enabled" for key in REQUIRED if settings.get(key) != "1"]
    errors += [f"{key} must NOT be enabled" for key in FORBIDDEN if settings.get(key) == "1"]
    if errors:
        raise ValueError("\n".join(errors))

    firmware_path = build / "firmware.bin"
    firmware = firmware_path.read_bytes()
    if len(firmware) < 1024 or firmware[0] != 0xE9:
        raise ValueError("firmware.bin is missing, truncated, or not an ESP application image")
    if b"wss-r2" not in firmware:
        raise ValueError("WSS r2 marker is not present in the firmware image")
    limits = []
    with (root / "min_spiffs.csv").open(encoding="utf-8") as handle:
        for row in csv.reader(line for line in handle if line.strip() and not line.lstrip().startswith("#")):
            if len(row) >= 5 and row[1].strip() == "app" and row[2].strip().startswith("ota_"):
                limits.append(number(row[4]))
    if len(limits) != 2:
        raise ValueError("Expected two OTA app partitions; review partition layout manually")
    if len(firmware) > min(limits):
        raise ValueError(f"Firmware ({len(firmware)} bytes) exceeds OTA slot ({min(limits)} bytes)")
    return {
        "patch": "wss-r2",
        "source_commit": os.environ.get("GITHUB_SHA") or subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "upstream_commit": "22523a71d789feee51d6c504a1575064a22a6cf2",
        "environment": "esp32-c3-internal",
        "firmware_bytes": len(firmware),
        "ota_slot_bytes": min(limits),
        "ota_space_remaining_bytes": min(limits) - len(firmware),
        "firmware_sha256": hashlib.sha256(firmware).hexdigest(),
        "required_sdk_flags": {key: settings[key] for key in REQUIRED},
        "insecure_tls_options": "disabled",
        "hardware_tested": False,
        "note": "Build checks are not a test of a real TLS handshake, eBUS operation or device stability.",
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    try:
        report = validate(args.root)
        text = json.dumps(report, indent=2) + "\n"
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(text, encoding="utf-8")
        print(text)
        print("WSS_BUILD_CHECKS_OK")
        return 0
    except (OSError, ValueError) as exc:
        print(f"WSS_BUILD_CHECK_FAILED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
