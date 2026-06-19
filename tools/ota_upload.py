#!/usr/bin/env python3
"""Upload firmware over FermentDial HTTP OTA (POST /firmware)."""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile


def usage() -> None:
    print(
        "Usage: ota_upload.py <firmware.bin> <host-or-url> [--password <pass>]",
        file=sys.stderr,
    )


def normalize_base(host: str) -> str:
    host = host.strip()
    if not re.match(r"^https?://", host, re.I):
        host = f"http://{host}"
    return host.rstrip("/")


def run_curl(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["curl", "-sS", *args],
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    args = sys.argv[1:]
    if len(args) < 2:
        usage()
        return 2

    firmware_path = args[0]
    host = args[1]
    password = os.environ.get("FERM_OTA_PASSWORD", "")

    if "--password" in args:
        idx = args.index("--password")
        if idx + 1 >= len(args):
            usage()
            return 2
        password = args[idx + 1]

    if not os.path.isfile(firmware_path):
        print(f"Firmware not found: {firmware_path}", file=sys.stderr)
        return 1

    base = normalize_base(host)
    cookie_file = None
    cookie_args: list[str] = []

    try:
        if password:
            cookie_file = tempfile.NamedTemporaryFile(delete=False, suffix=".cookies")
            cookie_file.close()
            login = run_curl(
                [
                    "-c",
                    cookie_file.name,
                    "-d",
                    f"password={password}",
                    "-o",
                    "/dev/null",
                    "-w",
                    "%{http_code}",
                    f"{base}/login",
                ]
            )
            if login.returncode != 0:
                print(login.stderr or login.stdout, file=sys.stderr)
                return login.returncode or 1
            if login.stdout.strip() not in {"302", "200"}:
                print(
                    f"Login failed (HTTP {login.stdout.strip() or 'unknown'})",
                    file=sys.stderr,
                )
                return 1
            cookie_args = ["-b", cookie_file.name]

        print(f"OTA upload to {base}/firmware ...")
        upload = run_curl(
            [
                "-f",
                *cookie_args,
                "-X",
                "POST",
                "-F",
                f"firmware=@{firmware_path}",
                f"{base}/firmware",
            ]
        )
        if upload.returncode != 0:
            print(upload.stderr or upload.stdout, file=sys.stderr)
            return upload.returncode or 1

        body = upload.stdout
        if "Update complete" in body:
            print("OTA upload complete. Device is rebooting.")
            return 0

        print(body, file=sys.stderr)
        print("OTA upload did not report success.", file=sys.stderr)
        return 1
    finally:
        if cookie_file is not None:
            os.unlink(cookie_file.name)


if __name__ == "__main__":
    raise SystemExit(main())