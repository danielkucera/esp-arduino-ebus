#!/usr/bin/env python3
import sys
import requests
from requests.auth import HTTPBasicAuth

def main():
    print("Custom HTTP Upload Script")
    if len(sys.argv) < 5:
        print("Usage: upload_http.py <IP> <username> <password> <firmware.bin>")
        sys.exit(1)

    ip = sys.argv[1]
    user = sys.argv[2]
    password = sys.argv[3]
    firmware_path = sys.argv[4]

    url = f"http://{ip}/firmware"

    print(f"Uploading {firmware_path} to {url} ...")

    with open(firmware_path, 'rb') as f:
        files = {"update": ("firmware.bin", f, "application/octet-stream")}
        response = requests.post(
            url,
            files=files,
            auth=HTTPBasicAuth(user, password),
            timeout=60
        )

    if response.status_code == 200:
        print("✔ Upload OK")
        print(response.text)
    else:
        print(f"✖ Upload FAILED: {response.status_code}")
        print(response.text)
        sys.exit(1)


if __name__ == "__main__":
    main()
