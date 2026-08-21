#!/usr/bin/env python3
"""
Pre-build script for PlatformIO.
Reads ha_profiles.json + ha_profiles_user.json and generates include/ha_profile_gen.hpp
with a static C++ array (base profiles merged with user overlay).
"""

import json
import os

try:
    Import("env")
    PROJECT_ROOT = os.path.dirname(env["PROJECT_SRC_DIR"])
except NameError:
    SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
    PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)

BASE_JSON = os.path.join(PROJECT_ROOT, "profiles", "ha_profiles.json")
OVERLAY_JSON = os.path.join(PROJECT_ROOT, "profiles", "ha_profiles_user.json")
OUTPUT_FILE = os.path.join(PROJECT_ROOT, "include", "ha_profile_gen.hpp")


def load_profiles(path):
    """Load profiles from JSON file, return empty list if file doesn't exist."""
    if not os.path.exists(path):
        return []
    with open(path, "r") as f:
        return json.load(f)


def generate():
    # Load base profiles
    base_profiles = load_profiles(BASE_JSON)

    # Load user overlay (extends base)
    overlay_profiles = load_profiles(OVERLAY_JSON)

    # Merge: user profiles override base by name
    merged = {p["name"]: p for p in base_profiles}
    for p in overlay_profiles:
        merged[p["name"]] = p  # user wins on name collision

    profiles = list(merged.values())

    lines = [
        "// AUTO-GENERATED FILE - DO NOT EDIT",
        "// Generated from profiles/ha_profiles.json + ha_profiles_user.json by scripts/generate_ha_profiles.py",
        "",
        "#pragma once",
        "",
        "#if defined(EBUS_INTERNAL)",
        "",
        '#include "ha_profile.hpp"',
        "",
        "namespace {",
        "// clang-format off",
        "//",
        "// HAProfile Registry (generated: base + user overlay)",
        "//",
        "constexpr HAProfile profiles[] = {",
    ]

    for p in profiles:
        name = p["name"]
        component = p["component"]
        device_class = p["device_class"]
        entity_category = p["entity_category"]
        mode = p["mode"]
        state_class = p["state_class"]
        step = p["step"]
        payload_on = p["payload_on"]
        payload_off = p["payload_off"]
        kvp = p["key_value_pairs"]
        kv_count = p["key_value_count"]
        default_key = p["default_key"]

        if kvp:
            pairs_str = ", ".join(
                f'{{{item["key"]}, "{item["value"]}"}}' for item in kvp
            )
            kvp_str = f'{{{{{pairs_str}}}}}'
        else:
            kvp_str = '{}'

        lines.append(
            f'    {{"{name}", "{component}", "{device_class}", "{entity_category}", "{mode}", "{state_class}", {float(step)}f, {payload_on}, {payload_off}, {kvp_str}, {kv_count}, {default_key}}},'
        )

    lines.extend([
        "};",
        "// clang-format on",
        "}  // namespace",
        "",
        "#endif",
        "",
    ])

    os.makedirs(os.path.dirname(OUTPUT_FILE), exist_ok=True)
    with open(OUTPUT_FILE, "w") as f:
        f.write("\n".join(lines))

    print(f"Generated {OUTPUT_FILE} with {len(profiles)} HA profiles (base: {len(base_profiles)}, overlay: {len(overlay_profiles)})")


generate()