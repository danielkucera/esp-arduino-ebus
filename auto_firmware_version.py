import subprocess
from pathlib import Path

Import("env")

def get_firmware_specifier_build_flag():
    force_version_file = Path("force_version.txt")
    if force_version_file.exists():
        build_version = force_version_file.read_text(encoding="utf-8").strip()
        if not build_version:
            raise ValueError("force_version.txt exists but is empty")
    else:
        ret = subprocess.run(
            ["git", "describe", "--tags", "--dirty"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )  # Uses any tags
        if ret.returncode == 0 and ret.stdout.strip():
            build_version = ret.stdout.strip()
        else:
            # Fallback for shallow/tagless CI checkouts
            ret = subprocess.run(
                ["git", "describe", "--always", "--dirty"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
            )
            build_version = ret.stdout.strip()
    build_flag = "-D AUTO_VERSION=\\\"" + build_version + "\\\""
    print ("Firmware Revision: " + build_version)
    return (build_flag)

env.Append(
    BUILD_FLAGS=[get_firmware_specifier_build_flag()]
)
