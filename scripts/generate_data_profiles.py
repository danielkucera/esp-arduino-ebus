#!/usr/bin/env python3
"""
Pre-build script for PlatformIO.
Reads data_profiles.json + data_profiles_user.json and generates include/data_profile_gen.hpp
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

BASE_JSON = os.path.join(PROJECT_ROOT, "profiles", "data_profiles.json")
OVERLAY_JSON = os.path.join(PROJECT_ROOT, "profiles", "data_profiles_user.json")
OUTPUT_FILE = os.path.join(PROJECT_ROOT, "include", "data_profile_gen.hpp")


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
        "// Generated from profiles/data_profiles.json + data_profiles_user.json by scripts/generate_data_profiles.py",
        "",
        "#pragma once",
        "",
        "#if defined(EBUS_INTERNAL)",
        "",
        '#include "data_profile.hpp"',
        "",
        "namespace {",
        "// clang-format off",
        "//",
        "// Data Profile Registry (generated: base + user overlay)",
        "//",
        "constexpr DataProfile profiles[] = {",
    ]

    for p in profiles:
        name = p["name"]
        datatype = p["datatype"]
        unit = p["unit"]
        divider = p["divider"]
        digits = p["digits"]
        min_val = p.get("min", 0)
        max_val = p.get("max", 0)
        lines.append(
            f'    {{"{name}", "{datatype}", "{unit}", {float(divider)}f, {digits}, {float(min_val)}f, {float(max_val)}f}},'
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

    print(f"Generated {OUTPUT_FILE} with {len(profiles)} data profiles (base: {len(base_profiles)}, overlay: {len(overlay_profiles)})")


generate()