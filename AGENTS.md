# Agent Notes

This repo is M5Stack Dial / ESP32-S3 firmware built with PlatformIO through
`uv`. Use the repo-local tooling; do not assume a global `pio` binary exists.

## Build And Verify

Use workspace-local caches so PlatformIO does not write to `~/.platformio`:

```sh
UV_CACHE_DIR=/home/delliott/projects/fermentdial/.uv-cache \
PLATFORMIO_HOME_DIR=/home/delliott/projects/fermentdial/.platformio \
uv run platformio run
```

Build all environments before calling a firmware change verified:

```sh
UV_CACHE_DIR=/home/delliott/projects/fermentdial/.uv-cache \
PLATFORMIO_HOME_DIR=/home/delliott/projects/fermentdial/.platformio \
uv run platformio run -e m5stack_dial -e m5stack_dial_demo -e m5stack_dial_wifi
```

Environments:

- `m5stack_dial`: real hardware, no Wi-Fi, no OTA.
- `m5stack_dial_demo`: default env, demo sensor, Wi-Fi, OTA.
- `m5stack_dial_wifi`: real sensor, Wi-Fi, OTA.

## Development Notes

- Keep hardware safety behavior conservative. Heater and pump must never be
  intentionally energized together, and demo sensor mode must not energize
  physical outputs.
- `include/secrets.h` is gitignored; use `include/secrets.example.h` as the
  reference for local Wi-Fi/MQTT settings.
- Check `git status --short` during reviews. New source files can be untracked
  and missing from `git diff`.
- Prefer scoped changes that match the existing Arduino/C++ style. Avoid broad
  rewrites in UI, storage, control, or network code unless the task requires it.
- When changing persisted settings, bump `SETTINGS_VERSION`, preserve old NVS
  reads where practical, and run the all-env build above.
