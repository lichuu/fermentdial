#!/usr/bin/env python3
"""Generate the GitHub Pages OTA + web-flash manifests.

Usage: gen_site_manifests.py <version> <owner/repo>
Expects firmware bins already placed in site/ota/ and site/flash/.
"""

import hashlib
import json
import sys

version, repo = sys.argv[1], sys.argv[2]

builds = {}
for variant in ("demo", "wifi"):
    path = f"site/ota/fermentdial-{variant}.bin"
    with open(path, "rb") as f:
        data = f.read()
    builds[variant] = {
        "file": f"fermentdial-{variant}.bin",
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }

ota = {
    "version": version,
    "notes_url": f"https://github.com/{repo}/releases/tag/v{version}",
    "builds": builds,
}
with open("site/ota/manifest.json", "w") as f:
    json.dump(ota, f, indent=2)

for variant, label in (("demo", "demo sensor"), ("wifi", "real sensor")):
    flash = {
        "name": f"FermentDial ({label})",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": "ESP32-S3",
            "improv": True,
            "parts": [{"path": f"fermentdial-{variant}-factory.bin", "offset": 0}],
        }],
    }
    with open(f"site/flash/{variant}.json", "w") as f:
        json.dump(flash, f, indent=2)

print(json.dumps(ota, indent=2))
