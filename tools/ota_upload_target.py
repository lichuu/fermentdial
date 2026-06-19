"""Route PlatformIO upload to HTTP OTA when upload_port is a network host."""

from __future__ import annotations

import re

Import("env")


def is_network_upload_target(port: str) -> bool:
    port = (port or "").strip()
    if not port or port.lower() == "auto":
        return False
    if port.startswith("/dev/"):
        return False
    if re.match(r"^COM\d+$", port, re.I):
        return False
    return True


upload_port = env.subst("$UPLOAD_PORT")
if is_network_upload_target(upload_port):
    env.Replace(
        UPLOADER="custom",
        UPLOAD_PROTOCOL="custom",
        UPLOADCMD=(
            '"$PYTHONEXE" "$PROJECT_DIR/tools/ota_upload.py" '
            f'"$SOURCE" "{upload_port}"'
        ),
    )