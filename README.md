# Dial Fermentation Controller

Custom appliance-style firmware for an M5Stack Dial v1.1 / ESP32-S3 fermentation temperature controller. It replaces a legacy Ss Brewtech FTSs controller and prioritizes local standalone operation; Wi-Fi, MQTT, Home Assistant discovery, and OTA are staged as clean future modules.

## Current Stage

- Stage 1: DS18B20 sensing, serial logging, output safety, pump protection, and control modes.
- Stage 2: Basic M5 Dial display, target adjustment, mode cycling, settings menu, NVS storage, and manual 5 second output tests.
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

Install PlatformIO, then build:

```sh
pio run
```

Flash and monitor:

```sh
pio run -t upload
pio device monitor
```

## Demo Sensor Mode

If the DS18B20 hardware is not attached yet, flash the demo environment:

```sh
pio run -e m5stack_dial_demo -t upload
```

Demo mode compiles with `FERM_DEMO_SENSOR=1`, generates a smooth simulated temperature around the setpoint, and shows `DEMO SENSOR` on the main screen. Physical heater and pump outputs are forced OFF in demo mode so simulated temperatures cannot energize hardware.

Use the normal environment for real fermentation control:

```sh
pio run -e m5stack_dial -t upload
```

Do not use demo sensor mode for actual fermentation control.

## Wi-Fi Setup And Web UI

Wi-Fi is optional. The local controller continues to run if Wi-Fi is unavailable.

For browser-based Wi-Fi setup without a DS18B20 attached, flash:

```sh
pio run -e m5stack_dial_demo_wifi -t upload
```

For real sensor hardware with Wi-Fi enabled, flash:

```sh
pio run -e m5stack_dial_wifi -t upload
```

If no Wi-Fi credentials are saved, the Dial starts a setup access point:

- SSID: `FermentDial-Setup`
- Password: `fermentdial`
- Setup page: `http://192.168.4.1/`
- Dashboard page: `http://192.168.4.1/dashboard`

Join that network from a phone or laptop, open the setup page, enter your home Wi-Fi credentials, and the device will save them to NVS and reboot. The setup page also links to the live dashboard, so demo Wi-Fi builds can show the browser UI without joining a home network.

After it joins your Wi-Fi, the Dial settings menu shows the assigned IP address under `Wi-Fi`. Open that IP address in a browser to see the local web UI. From the Dial settings menu, selecting `Wi-Fi` starts the setup AP again even if credentials are already saved.

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

## First Flash

1. Wire the DS18B20 to GPIO13, 3.3V, and GND.
2. Wire MOSFET trigger grounds to M5 GND.
3. Leave the 12V load power disconnected for the first boot.
4. Flash the firmware.
5. Confirm the display boots and serial logging shows `heater=OFF pump=OFF`.
6. Connect 12V load power only after the output safety checklist passes.

## Controls

- Main screen rotation: adjust target temperature in 0.1 F increments.
- Short press: cycle `OFF -> AUTO -> HEAT_ONLY -> COOL_ONLY -> OFF`.
- Double short press: quick toggle between `OFF` and `AUTO`.
- Long press: open or leave the settings menu.
- In settings: rotate to move or change values, short press to select/confirm.
- Menu timeout returns to the main screen after inactivity.

## Modes

- `OFF`: heater OFF, pump OFF. OFF mode overrides everything.
- `AUTO`: heats below target minus hysteresis; cools above target plus hysteresis.
- `HEAT_ONLY`: heater may run on low-temperature demand; pump remains off in normal control.
- `COOL_ONLY`: pump may run on high-temperature demand; heater remains off in normal control.

Runtime states are `BOOT`, `OFF`, `IDLE`, `HEATING`, `COOLING`, and `FAULT`.

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
- Sensor unplugged causes FAULT and outputs OFF.
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

MQTT is not enabled in this Stage 1/2 firmware. The planned topic layout is:

| Topic | Direction | Payload |
| --- | --- | --- |
| `fermentdial/state` | publish | JSON state: `temperature`, `target`, `unit`, `mode`, `runtime_state`, `heater`, `pump`, `fault` |
| `fermentdial/state/temperature` | publish | current temperature in the published `unit` |
| `fermentdial/state/target` | publish | target temperature in the published `unit` |
| `fermentdial/state/unit` | publish | `F` or `C` |
| `fermentdial/state/mode` | publish | `OFF`, `AUTO`, `HEAT_ONLY`, `COOL_ONLY` |
| `fermentdial/state/runtime_state` | publish | `BOOT`, `OFF`, `IDLE`, `HEATING`, `COOLING`, `FAULT` |
| `fermentdial/state/heater` | publish | `ON` or `OFF` |
| `fermentdial/state/pump` | publish | `ON` or `OFF` |
| `fermentdial/state/fault` | publish | `NONE`, `SENSOR`, or `INTERLOCK` |
| `fermentdial/set/target` | subscribe | JSON `{"value":68.0,"unit":"F"}` or `{"value":20.0,"unit":"C"}` |
| `fermentdial/set/mode` | subscribe | new user mode |

MQTT commands will only mutate settings. The controller still applies sensor fault, OFF mode, pump timing, and output interlock rules locally.

## OTA Plan

OTA is disabled by default. When enabled later, update/restart hooks must force both outputs OFF before flash or reboot. The placeholder is in `src/network.cpp`.
