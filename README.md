# Dial Fermentation Controller

Custom appliance-style firmware for an M5Stack Dial v1.1 / ESP32-S3 fermentation temperature controller. It prioritizes local standalone operation; Wi-Fi, MQTT, Home Assistant discovery, and OTA are staged as clean future modules.

## Current Stage

- Stage 1: DS18B20 sensing, serial logging, output safety, pump protection, and control modes.
- Stage 2: Basic M5 Dial display, profile target adjustment, mode cycling, settings menu, NVS storage, and manual 5 second output tests.
- Stage 4/5 placeholders: MQTT/Home Assistant discovery and OTA hooks are isolated in `src/network.cpp`.

## Hardware

| Connection | Wiring |
| --- | --- |
| GPIO13 | DS18B20 DATA |
| DS18B20 VCC | 3.3V only |
| DS18B20 GND | GND |
| GPIO2 | heater MOSFET trigger input |
| GPIO1 | pump MOSFET trigger input |
| M5 GND | MOSFET trigger ground |
| 12V input | 7.5A fuse -> +12V bus |
| heater/pump positive | +12V bus |
| heater/pump negative | MOSFET switched negative |

Important electrical notes:

- The DS18B20 adapter board has a built-in 4.7k pull-up marked `472`. Power it from 3.3V only, because DATA is pulled up to VCC.
- The AOD4184 MOSFET trigger inputs are active HIGH.
- The heater pad and cooling pump are low-side switched 12V DC loads.
- The firmware never intentionally allows heater and pump to run at the same time.

## PlatformIO Setup

Use `uv` to install and run PlatformIO from `pyproject.toml`:

```sh
uv run platformio run
```

Flash and monitor:

```sh
uv run platformio run -t upload
uv run platformio device monitor
```

## VLW Fonts

The repo includes a local M5GFX VLW converter backed by `freetype-py`.
The default glyph set is printable ASCII plus the degree symbol (`0xB0`):

```sh
uv run python tools/make_vlw_font.py \
  --font /path/to/DejaVuSans-Bold.ttf \
  --size 44 \
  --output data/fonts/dejavu_sans_bold_44.vlw
```

## Demo Sensor Mode

If the DS18B20 hardware is not attached yet, flash the demo environment:

```sh
uv run platformio run -e m5stack_dial_demo -t upload
```

Demo mode compiles with `FERM_DEMO_SENSOR=1` and `FERM_ENABLE_NETWORK=1`,
generates a smooth simulated temperature around the setpoint, and shows
`DEMO SENSOR` on the main screen. Physical heater and pump outputs are forced
OFF in demo mode so simulated temperatures cannot energize hardware. The demo
build also serves the browser dashboard.

Use the normal environment for real fermentation control:

```sh
uv run platformio run -e m5stack_dial -t upload
```

Do not use demo sensor mode for actual fermentation control.

## Wi-Fi Setup And Web UI

Wi-Fi is optional for real hardware. The local controller continues to run if
Wi-Fi is unavailable. Demo firmware always includes Wi-Fi so the browser
dashboard is available without a sensor attached.

For real sensor hardware with Wi-Fi enabled, flash:

```sh
uv run platformio run -e m5stack_dial_wifi -t upload
```

If no Wi-Fi credentials are saved, the Dial starts a setup access point:

- SSID: `FermentDial-Setup-XXXX`
- Password: `fermentdial`
- Setup page: `http://192.168.4.1/`
- Dashboard page: `http://192.168.4.1/dashboard`

Join that network from a phone or laptop, open the setup page, scan for nearby Wi-Fi networks or enter the network name manually, then enter the password. The device saves the credentials to NVS and reboots. The setup page also links to the live dashboard, so demo Wi-Fi builds can show the browser UI without joining a home network.

After it joins your Wi-Fi, the Dial settings menu shows the assigned IP address under `Wi-Fi`. Open that IP address in a browser to see the local web UI. The DHCP hostname is `fermentdial-XXXX` by default, or `fermentdial-{fermenter-name}-XXXX` after the fermenter is named. From the Dial settings menu, selecting `Wi-Fi` starts the setup AP again even if credentials are already saved.

Credentials can also be compiled in by copying:

```sh
cp include/secrets.example.h include/secrets.h
```

Then edit `include/secrets.h`. That file is gitignored.

The project uses:

- Arduino framework for ESP32-S3
- `m5stack/M5Dial`
- `m5stack/M5Unified`
- `m5stack/M5GFX`
- `paulstoffregen/OneWire`
- `milesburton/DallasTemperature`

The PlatformIO board is set to `m5stack-stamps3`, matching the StampS3 inside the M5Stack Dial.

## Web UI Development

The dashboard and settings pages are a Svelte app in `webui/`, built with Vite
and compiled into the firmware binary (no LittleFS, no second OTA step). The
firmware serves the built bundle from `/app.js` and `/app.css`; `src/web_assets.h`
is the generated, committed C++ header that embeds it.

If you change anything under `webui/src/`, rebuild and regenerate that header
before building firmware:

```sh
cd webui
npm install
npm run build
```

This runs `vite build` and then `webui/tools/build-header.mjs`, which gzips
`webui/dist/app.js`/`app.css` and writes `src/web_assets.h`. Commit the
regenerated header along with your `webui/` changes — PlatformIO itself never
invokes Node, so `uv run platformio run` works without `webui/` being built.

For local iteration with hot reload against a live device, run:

```sh
DEVICE_HOST=192.168.x.x npm run dev
```

from `webui/`. This proxies `/api`, `/settings`, `/wifi`, `/login`, `/logout`,
and `/metrics` requests to that device so the dev server's `fetch` calls reach
real firmware data.

## First Flash

1. Wire the DS18B20 to GPIO13, 3.3V, and GND.
2. Wire MOSFET trigger grounds to M5 GND.
3. Leave the 12V load power disconnected for the first boot.
4. Flash the firmware.
5. Confirm the display boots and serial logging shows `heater=OFF pump=OFF`.
6. Connect 12V load power only after the output safety checklist passes.

## Profiles And Controls

- FermentDial stores six profiles: `Ferment`, `Soft Crash`, `Crash`, `Lager`, `Custom 1`, and `Custom 2`.
- The Dial settings menu can select the active profile and adjust that profile's target.
- The web dashboard can rename profiles and edit every profile target.
- Main screen rotation adjusts the active profile target in 0.1 F or 0.1 C increments.
- Tap the display from the main screen for the quick profile/mode picker.
- Press the knob from the main screen to open settings.
- In settings: rotate to move or change values, short press to select/confirm.
- Menu timeout returns to the main screen after inactivity.

## Diacetyl Rest

- D-rest temporarily overrides the live setpoint with a configured rest target.
- Defaults are 70 F for 48 hours, with duration selectable in 24 hour increments from 24 to 96 hours.
- The web dashboard can start/stop a rest and edit the rest target, duration, and return profile.
- The Dial settings menu includes `D-Rest`; tap it to start the configured rest, or tap again while active to finish it and apply the configured return profile.
- When the timer completes, FermentDial switches to the configured return profile and recalls that profile's target.
- If power drops mid-rest, the remaining time resumes from the last saved checkpoint. Without an RTC, outage time is not counted.

## Brewfather Custom Stream

Wi-Fi builds can publish to Brewfather from the web UI under
`Settings -> Monitoring -> Brewfather`.

- Enable Custom Stream in Brewfather under `Settings -> Power-ups -> Custom Stream`.
- Paste the logging ID into FermentDial, or paste the full stream URL and it will extract the `id=` value.
- FermentDial posts controller temperature, target temperature, runtime state, and selected hydrometer gravity/battery/RSSI when present.
- Set `Payload` to `Hydrometer only` to forward each fresh discovered Tilt/RAPT reading as its own Brewfather device. In that mode Brewfather receives hydrometer temperature/gravity/battery/RSSI without the controller target or runtime state.
- Brewfather ignores posts more frequent than one per 15 minutes per device name, so the firmware enforces a 15 minute minimum interval.

## Modes

- `OFF`: heater OFF, pump OFF. OFF mode overrides everything.
- `AUTO`: cools above the active profile target plus `Cool on`; heats below target minus `Heat on`.
- `HEAT_ONLY`: heater may run on low-temperature demand; pump remains off in normal control.
- `COOL_ONLY`: pump may run on high-temperature demand; heater remains off in normal control.

Runtime states are `BOOT`, `OFF`, `IDLE`, `HEATING`, `COOLING`, and `FAULT`. The web UI displays crash-profile cooling as `CRASHING`.

The default control band uses conservative fermentation defaults: cooling starts at target + 0.5 F, heating starts at target - 5.0 F, and active outputs release near the target hold band. Pump min-off/min-run protection still applies.

## Pump Protection

- Pump cannot start until it has been off for `pumpMinOffSeconds`.
- Once the pump starts, it remains on for `pumpMinRunSeconds`.
- Sensor fault and OFF mode override the minimum run timer.
- On boot, the pump is treated as newly off, so the minimum off timer protects it before the first cooling cycle.

## Output Test Mode

The settings menu includes hidden/manual bench tests:

- `Test heater`: heater trigger ON for 5 seconds.
- `Test pump`: pump trigger ON for 5 seconds.
- Tests require confirmation.
- Tests are refused in OFF mode or sensor fault.
- Tests never run both outputs at once.
- Exiting or timing out leaves outputs OFF.

## Safety Test Checklist

- Boot with outputs OFF.
- Sensor unplugged or three consecutive bad readings cause FAULT and outputs OFF.
- Too cold turns only heater ON.
- Too warm turns only pump ON.
- Within deadband turns both OFF.
- OFF mode keeps both OFF.
- HEAT_ONLY never turns pump ON during normal control.
- COOL_ONLY never turns heater ON during normal control.
- Pump min-off timer works.
- Pump min-run timer works.
- Wi-Fi/MQTT loss does not stop local control.
- Restart during heating/cooling turns outputs OFF during boot.

## MQTT Topics

Wi-Fi builds can publish MQTT from the web UI under
`Settings -> Monitoring -> MQTT / Home Assistant`.

| Topic | Direction | Payload |
| --- | --- | --- |
| `fermentdial/availability` | publish | retained `online`/`offline` (LWT) |
| `fermentdial/state` | publish | retained controller status JSON (`Controller + hydrometer` mode) |
| `fermentdial/hydrometer/<id>/state` | publish | retained per-hydrometer JSON (`Hydrometer only` mode) |
| `<discovery_prefix>/sensor/fermentdial_<id>/<metric>/config` | publish | retained Home Assistant discovery config |

The MQTT `Payload` setting defaults to `Controller + hydrometer`, which publishes the same status JSON used by the web dashboard to `fermentdial/state`: temperature, target, unit, profile data, mode/runtime state, outputs, faults, and hydrometer details.

Set `Payload` to `Hydrometer only` to act as a multi-hydrometer bridge. Each discovered Tilt/RAPT gets its own retained state topic (`fermentdial/hydrometer/<id>/state`) carrying a compact `{gravity, temp, rssi, battery, abv, velocity, stable_s, age_s}` document. When `Publish Home Assistant discovery` is enabled, FermentDial also retains discovery configs under the configurable discovery prefix (default `homeassistant`) so each hydrometer auto-appears in HA as its own device — with gravity, temperature, ABV, gravity velocity, stable time, battery, and signal entities — nested beneath the FermentDial bridge. Staleness is handled by `expire_after`: a hydrometer that stops advertising goes unavailable in HA on its own, and discovery configs are re-announced on every reconnect. ABV, original gravity, and stability are only tracked for the hydrometer selected in settings; other bridged devices report those as null/zero.

MQTT commands are not subscribed yet. Future commands must only mutate settings; the controller still applies sensor fault, OFF mode, pump timing, and output interlock rules locally.

## OTA Plan

OTA is disabled by default. When enabled later, update/restart hooks must force both outputs OFF before flash or reboot. The placeholder is in `src/network.cpp`.
