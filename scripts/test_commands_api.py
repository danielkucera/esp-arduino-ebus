#!/usr/bin/env python3
"""Integration test runner for /api/v1/commands* endpoints.

This script targets firmware HTTP endpoints and validates core command-storage flows:
- evaluate
- insert
- list
- save
- remove (partial and all)
- load
- wipe

Usage:
  python3 scripts/test_commands_api.py --base-url http://192.168.1.50
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from typing import Any, Dict, List, Optional


SAMPLE_COMMANDS: List[Dict[str, Any]] = [
    {
        "key": "01",
        "name": "temp01",
        "read_cmd": "08b5110101",
        "write_cmd": "0000",
        "active": False,
        "interval": 0,
        "master": False,
        "position": 1,
        "datatype": "DATA1C",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "\u00b0C",
        "ha": True,
        "ha_component": "sensor",
        "ha_device_class": "temperature",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "02",
        "name": "temp02",
        "read_cmd": "08b5110101",
        "write_cmd": "0000",
        "active": False,
        "interval": 0,
        "master": False,
        "position": 2,
        "datatype": "DATA1C",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "\u00b0C",
        "ha": True,
        "ha_component": "sensor",
        "ha_device_class": "temperature",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "03",
        "name": "pumpstate",
        "read_cmd": "08b5110101",
        "write_cmd": "0000",
        "active": False,
        "interval": 0,
        "master": False,
        "position": 7,
        "datatype": "UINT8",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 1,
        "unit": "",
        "ha": True,
        "ha_component": "sensor",
        "ha_device_class": "power",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "04",
        "name": "storageTemp",
        "read_cmd": "08b509030d1700",
        "write_cmd": "0000",
        "active": True,
        "interval": 30,
        "master": False,
        "position": 1,
        "datatype": "DATA2C",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "\u00b0C",
        "ha": True,
        "ha_component": "sensor",
        "ha_device_class": "temperature",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "05",
        "name": "DisplayedRoomTemp",
        "read_cmd": "15b509030d3b00",
        "write_cmd": "",
        "active": True,
        "interval": 30,
        "master": False,
        "position": 1,
        "datatype": "DATA2C",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "\u00b0C",
        "ha": True,
        "ha_component": "sensor",
        "ha_device_class": "temperature",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "06",
        "name": "DHWTempSet",
        "read_cmd": "15b509030d4000",
        "write_cmd": "15b509050e4000",
        "active": True,
        "interval": 60,
        "master": False,
        "position": 1,
        "datatype": "DATA2C",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "\u00b0C",
        "ha": True,
        "ha_component": "sensor",
        "ha_device_class": "temperature",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "07",
        "name": "HeatingTempSet",
        "read_cmd": "15b509030d2200",
        "write_cmd": "15b509050e2200",
        "active": True,
        "interval": 60,
        "master": False,
        "position": 1,
        "datatype": "DATA2C",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "\u00b0C",
        "ha": True,
        "ha_component": "sensor",
        "ha_device_class": "temperature",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "08",
        "name": "PrEnergySumHc1",
        "read_cmd": "08b509030df500",
        "write_cmd": "",
        "active": True,
        "interval": 9_999_999,
        "master": False,
        "position": 1,
        "datatype": "UINT32",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "",
        "ha": False,
        "ha_component": "",
        "ha_device_class": "",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
    {
        "key": "09",
        "name": "CurrentError",
        "read_cmd": "08b503020001",
        "write_cmd": "",
        "active": True,
        "interval": 30,
        "master": False,
        "position": 1,
        "datatype": "UINT8",
        "divider": 1,
        "min": 1,
        "max": 100,
        "digits": 3,
        "unit": "",
        "ha": False,
        "ha_component": "",
        "ha_device_class": "",
        "ha_entity_category": "",
        "ha_mode": "auto",
        "ha_key_value_map": {},
        "ha_default_key": 0,
        "ha_payload_on": 1,
        "ha_payload_off": 0,
        "ha_state_class": "",
        "ha_step": 1,
    },
]


@dataclass
class HttpResult:
    status: int
    body: str
    error: Optional[str] = None


class ApiClient:
    def __init__(self, base_url: str, timeout: float) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def request(self, method: str, path: str, payload: Optional[Any] = None) -> HttpResult:
        url = urllib.parse.urljoin(self.base_url + "/", path.lstrip("/"))
        data: Optional[bytes] = None
        headers = {"Accept": "application/json, text/plain, */*"}

        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"

        req = urllib.request.Request(url=url, data=data, headers=headers, method=method)

        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as response:
                body = response.read().decode("utf-8", errors="replace")
                return HttpResult(status=response.status, body=body)
        except urllib.error.HTTPError as err:
            body = err.read().decode("utf-8", errors="replace")
            return HttpResult(status=err.code, body=body)
        except (ConnectionResetError, socket.timeout, TimeoutError, urllib.error.URLError, OSError) as err:
            return HttpResult(status=0, body="", error=f"{type(err).__name__}: {err}")


class TestRunner:
    def __init__(self, client: ApiClient, verbose: bool = False) -> None:
        self.client = client
        self.verbose = verbose
        self.failures: List[str] = []

    def check(self, condition: bool, message: str) -> None:
        if condition:
            print(f"PASS: {message}")
        else:
            print(f"FAIL: {message}")
            self.failures.append(message)
            raise RuntimeError(message)

    def expect_http_200(self, result: HttpResult, operation: str) -> None:
        self.check(result.status == 200, f"{operation} returned HTTP 200 (got {result.status})")
        if result.error:
            self.check(False, f"{operation} request error: {result.error}")
        if self.verbose:
            print(f"  body: {result.body}")
            if result.error:
                print(f"  error: {result.error}")

    def parse_commands(self, result: HttpResult, operation: str) -> List[Dict[str, Any]]:
        self.expect_http_200(result, operation)
        try:
            parsed = json.loads(result.body)
        except json.JSONDecodeError as exc:
            self.check(False, f"{operation} returned valid JSON ({exc})")
            return []
        self.check(isinstance(parsed, list), f"{operation} returned a JSON array")
        return parsed if isinstance(parsed, list) else []

    def run(self) -> int:
        keys = [item["key"] for item in SAMPLE_COMMANDS]

        print("Running /api/v1/commands* integration checks")
        print(f"Target: {self.client.base_url}")
        try:
            # Clean start so this test is deterministic.
            wipe_start = self.client.request("POST", "/api/v1/commands/wipe")
            self.expect_http_200(wipe_start, "POST /api/v1/commands/wipe (initial)")

            evaluate = self.client.request("POST", "/api/v1/commands/evaluate", SAMPLE_COMMANDS)
            self.expect_http_200(evaluate, "POST /api/v1/commands/evaluate")
            self.check("Ok" in evaluate.body, "evaluate returned success text")

            insert = self.client.request("POST", "/api/v1/commands/insert", SAMPLE_COMMANDS)
            self.expect_http_200(insert, "POST /api/v1/commands/insert")
            self.check("Ok" in insert.body, "insert returned success text")

            listed_after_insert = self.parse_commands(
                self.client.request("GET", "/api/v1/commands"),
                "GET /api/v1/commands (after insert)",
            )
            listed_keys = sorted(str(item.get("key", "")) for item in listed_after_insert)
            self.check(
                len(listed_after_insert) == len(SAMPLE_COMMANDS),
                "commands list length matches inserted sample",
            )
            self.check(
                listed_keys == sorted(keys),
                "commands list contains expected keys after insert",
            )

            save = self.client.request("POST", "/api/v1/commands/save")
            self.expect_http_200(save, "POST /api/v1/commands/save")

            remove_partial = self.client.request(
                "POST",
                "/api/v1/commands/remove",
                {"keys": ["01", "02"]},
            )
            self.expect_http_200(remove_partial, "POST /api/v1/commands/remove (partial)")
            self.check("Ok" in remove_partial.body, "partial remove returned success text")

            listed_after_partial_remove = self.parse_commands(
                self.client.request("GET", "/api/v1/commands"),
                "GET /api/v1/commands (after partial remove)",
            )
            self.check(
                len(listed_after_partial_remove) == len(SAMPLE_COMMANDS) - 2,
                "commands list length decreased by two after partial remove",
            )

            load = self.client.request("POST", "/api/v1/commands/load")
            self.expect_http_200(load, "POST /api/v1/commands/load")

            listed_after_load = self.parse_commands(
                self.client.request("GET", "/api/v1/commands"),
                "GET /api/v1/commands (after load)",
            )
            loaded_keys = sorted(str(item.get("key", "")) for item in listed_after_load)
            self.check(
                len(listed_after_load) == len(SAMPLE_COMMANDS),
                "commands list length restored after load",
            )
            self.check(
                loaded_keys == sorted(keys),
                "commands keys restored after load",
            )

            remove_all = self.client.request("POST", "/api/v1/commands/remove", {})
            self.expect_http_200(remove_all, "POST /api/v1/commands/remove (all)")
            self.check("Ok" in remove_all.body, "remove all returned success text")

            listed_after_remove_all = self.parse_commands(
                self.client.request("GET", "/api/v1/commands"),
                "GET /api/v1/commands (after remove all)",
            )
            self.check(len(listed_after_remove_all) == 0, "commands list is empty after remove all")

            wipe_end = self.client.request("POST", "/api/v1/commands/wipe")
            self.expect_http_200(wipe_end, "POST /api/v1/commands/wipe (final)")
        except RuntimeError:
            print()
            print("Summary: failed fast on first error")
            return 1

        print()
        print("Summary: all checks passed")
        return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Test /api/v1/commands* endpoints against a running device"
    )
    parser.add_argument(
        "--base-url",
        default="http://esp-ebus.local",
        help="Base URL for firmware web server (default: %(default)s)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=8.0,
        help="HTTP timeout in seconds (default: %(default)s)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print response bodies for each request",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    client = ApiClient(base_url=args.base_url, timeout=args.timeout)
    runner = TestRunner(client=client, verbose=args.verbose)
    return runner.run()


if __name__ == "__main__":
    raise SystemExit(main())
