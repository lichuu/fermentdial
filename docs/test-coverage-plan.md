# Test Coverage Before the Decomposition Refactor

## Context

We're about to decompose three large files (`network.cpp` 3234, `ui.cpp` 2489,
`config.h` 958, + optional `main.cpp` policy extraction) into focused units. That work is
pure code-motion, so its dominant failure modes — broken linkage, ODR violations,
mis-duplicated `#if FERM_ENABLE_NETWORK` guards — are already caught by the all-env
compile+link. What compile+link does **not** catch: (a) a "motion" that wasn't actually pure
(someone tidies logic while moving it), and (b) there's no durable behavioral net for the
feature changes the decomposition is meant to *enable* afterward.

This plan adds that net. The codebase splits cleanly into three testability tiers, each with a
different (and appropriate) tactic. All three are in scope.

What this deliberately does **not** cover: `ui.cpp` display rendering — it's TFT framebuffer
output with no cheap host-side capture path. That split leans on compile+link plus the manual
UI smoke checklist below. Honest gap, called out so it isn't mistaken for covered.

Findings below were verified against the source (three Explore passes); line numbers are current.

---

## Tier 2 — Native unit tests for pure logic (`pio test -e native`)

The highest-value, most durable asset, and it directly de-risks the `config.h → programs.h`
split. **All target logic in `config.h` is header-only `inline`**, so these tests compile just
`config.h` against a tiny shim — no firmware `.cpp`, no hardware.

**`platformio.ini` — add a host env (must override the global `[env]` espressif defaults):**
```ini
[env:native]
platform = native
test_framework = unity
lib_deps =                 ; override [env] — do NOT inherit M5Dial/OneWire/Dallas/NimBLE
build_flags =
  -std=gnu++17
  -I include
  -I test/mocks
  -D FERM_ENABLE_NETWORK=0
  -D FERM_DEMO_SENSOR=0
```
`test_build_src` stays at its default (`no`) for the settings suite — header-only.

**`test/mocks/Arduino.h`** — minimal shim (surface mapped exactly from config.h usage):
- A `String` class supporting only: ctor from `const char*`, copy ctor/assign, assign from
  `const char*`, `operator==(const char*)`, `length()`, `trim()`→self, `toUpperCase()`→self,
  `substring(start,end)`, `startsWith(const char*)`. (Back it with `std::string`.)
- `uint8_t/uint16_t/uint32_t` via `<cstdint>`; pull `isnan`/`NAN`/`roundf` from `<cmath>`.
- For Tier 3 (shared file): `pinMode`/`digitalWrite`/`OUTPUT`/`INPUT`/`HIGH`/`LOW`, where
  `digitalWrite` records into a `mock_gpio[]` table the tests can read.

**`test/test_settings/test_settings.cpp`** — `#include "config.h"`, Unity cases on the pure
helpers (signatures confirmed pure, no String I/O unless noted):
- `sanitizeSettings` (config.h:793) — characterization: feed a deliberately out-of-range
  `Settings` and assert it normalizes — brightness/deltas/pump-min-times/gravity clamped,
  `fermenterName`/profile names trimmed+truncated, temps snapped to the display grid, program
  state made consistent. This is the single most valuable test (it's the load-path normalizer).
- `computeStepTargetC` (571) — ramp interpolation at frac 0 / 0.5 / 1 / overshoot, plus crash,
  hold, manualWait, and the zero-duration guard.
- `effectiveStepExit` (565), `startProgram`/`stopProgram` (603/595) state transitions.
- Unit conversions: `cToF`/`fToC` round-trip, `toDisplayTemp`/`fromDisplayTemp`,
  `snapTempC`/`snapDeltaC` grid behavior.
- `clampFloat`/`clampU32`/`clampBrightness` edges.

**Run:**
```sh
UV_CACHE_DIR="$PWD/.uv-cache" \
PLATFORMIO_HOME_DIR="$PWD/.platformio" \
uv run platformio test -e native
```

---

## Tier 1 — HTTP golden-master (against the demo build)

The exporters / JSON builders (`statusJson`, `settingsConfigJson`, `programJson`,
`metricsText`) are entangled with `WebServer`/`WiFi`/`LittleFS` — mocking them natively isn't
worth it. Instead, snapshot their **real HTTP output** before and after the refactor and diff.
This is the net for the biggest, riskiest file (`network.cpp`).

**Prerequisite:** flash the *current* `m5stack_dial_demo` to the dev device (192.168.30.48)
**before** starting the refactor — the baseline must come from pre-refactor firmware. Demo env
populates simulated sensor/hydrometer data and forces physical outputs off, so endpoints return
full payloads without real hardware. Auth note: when no admin password is set, `isAuthed()`
returns true (network.cpp:1037) and all endpoints are open; the script handles the
password-set case by logging in (`POST /login`, capture `fdsession` cookie) for the two
auth-gated reads.

**`test/golden/capture.sh <host>`** — curl each endpoint, normalize, write to an output dir:
- **Clean, no masking** (best golden signal): `/api/program` (no volatile fields), and the
  server-rendered HTML shells `/`, `/settings`, `/firmware`, `/login`.
- **Light mask** (`jq`): `/api/settings` — drop `.wifiSsid`, `.wifiIp`, and
  `.influx.lastStatus` / `.mqtt.lastStatus` / `.brewfather.lastStatus`.
- **Heavy mask** (`jq` filter): `/api/status` — null out the volatile set
  (`uptimeSeconds`, `clock`, `temperature`, `target`, `program.step*Seconds`,
  `diacetylRest.remaining*`, `ip`, `wifiStatus`, and the hydrometer live fields
  `rssi`/`temperature`/`gravity`/`gravityVelocity`/`stableSeconds`/`lastSeenSeconds`/`batteryV`,
  including inside `hydrometerDevices[]`). What survives — the structure, keys, profile/program
  shape, units, enum strings — is exactly what the refactor could break.
- **`/metrics`**: normalize by stripping the numeric value but **keeping `name{labels}`** per
  line, then sort. Protects the series/label structure (the format) while ignoring live values.
- **Skip** `/api/history`, `/api/history.csv`, `/api/events`, `/api/selfcheck` — fully volatile,
  low format-regression risk, high diff noise. Skip is explicit/logged, not silent.

**Validate the mask is deterministic first:** capture twice against the *same unchanged build*
and confirm `before/ == before2/`. A non-empty self-diff means the mask is incomplete — fix it
before trusting the tool across the refactor.

**Workflow:** capture → `golden/before/` ; refactor ; re-flash demo ; capture → `golden/after/`
; `diff -ru before after`. Empty diff ⇒ HTTP behavior preserved.

**Round-trip (last, opt-in):** `POST /api/settings` a known body, then `GET /api/status` and
assert the field is reflected (masked); restore afterward. Note: this writes NVS (`fermctl`).

---

## Tier 3 — Controller safety tests (native)

Pins the CLAUDE.md safety contract directly: heater+pump never both energized, sensor-fault
forces outputs off, demo mode never drives physical pins. `FermentationController` is
well-factored for this — time is fully injected via `nowMs` (no internal `millis()`), and the
sensor enters only as `bool sensorValid` + `float tempC`, so the invariants are assertable
through the public getters (`heaterOn`/`pumpOn`/`runtimeState`/`faultCode`) without touching the
private `applyOutputs`.

One wrinkle: `control.cpp`'s translation unit pulls `<OneWire.h>`/`<DallasTemperature.h>` for
the sibling `TemperatureSensor`, so a native build needs stub headers.

**`platformio.ini` — second env for the demo-guard build** (the demo output-suppression is
compile-time `#if FERM_DEMO_SENSOR`, so it needs its own build):
```ini
[env:native_demo]
extends = env:native
build_flags = ${env:native.build_flags} -U FERM_DEMO_SENSOR -D FERM_DEMO_SENSOR=1
build_src_filter = -<*> +<control.cpp>
test_build_src = yes
```
Add the same `build_src_filter`/`test_build_src` to `[env:native]` so `control.cpp` compiles for
the non-demo control suite too. (Settings suite ignores the extra TU.)

**Stub headers** `test/mocks/OneWire.h` + `test/mocks/DallasTemperature.h` — inline no-op classes
covering exactly the methods `TemperatureSensor` calls (enumerate with
`grep -nE '_sensors\.|_oneWire\.' src/control.cpp`; stub each as an inline returning a benign
value). The `mock_gpio[]` recorder in `Arduino.h` lets the demo test observe pin levels.

**`test/test_control/test_control.cpp`** (interlock/fault under `native`, suppression under
`native_demo`):
- Both heater and pump requested → `runtimeState()==Fault`, `faultCode()==Interlock`, both off
  (control.cpp:223).
- `sensorValid=false` → both off, `faultCode()==Sensor` (control.cpp:122).
- Pump min-run / min-off windows by stepping injected `nowMs` (control.cpp:260/272).
- **Demo build:** drive `update()` so the logic wants the heater on; assert `heaterOn()` reflects
  the *logical* decision **but** `mock_gpio[PIN_HEATER_TRIGGER]` stayed at the off level
  (physical never energized — control.cpp:244).

---

## Files to create / modify

**Modify:** `platformio.ini` (add `[env:native]`, `[env:native_demo]`).

**Create:**
- `test/mocks/Arduino.h` (String shim + GPIO recorder)
- `test/mocks/OneWire.h`, `test/mocks/DallasTemperature.h` (inline no-op stubs)
- `test/test_settings/test_settings.cpp`
- `test/test_control/test_control.cpp`
- `test/golden/capture.sh` (+ inline `jq` normalize filters)

**Reuse (don't reimplement):**
- `config.h` inline helpers — tested directly (header-only).
- `control.h` getters — the assertion surface; `nowMs` injected.
- Live HTTP endpoints + their JSON/text builders — exercised over HTTP, not duplicated.

---

## Verification

1. **Native suites:** `uv run platformio test -e native` and `-e native_demo` → all Unity cases
   pass. Confirm from the build log that the native envs pull **no** espressif/M5/OneWire libs
   (the `lib_deps =` override worked).
2. **Golden-master self-check:** capture twice against one unchanged demo build; `before/` vs
   `before2/` must be an empty diff (proves the masking is deterministic) before relying on it.
3. **Don't perturb firmware builds:** the test additions must not break the device envs — run the
   full all-env build from CLAUDE.md:
   ```sh
   UV_CACHE_DIR="$PWD/.uv-cache" \
   PLATFORMIO_HOME_DIR="$PWD/.platformio" \
   uv run platformio run -e m5stack_dial -e m5stack_dial_demo -e m5stack_dial_wifi
   ```
4. **Success criteria:** native suites green on both envs; golden-master self-diff empty; all
   three device envs still build. The net is then in place — run the golden capture to
   `golden/before/` immediately before the decomposition starts.
5. **UI smoke:** after any UI change, run the manual checklist below on real hardware (one
   firmware env is enough; `m5stack_dial_demo` is the usual choice).

---

## Manual UI smoke (required after any UI change)

Display rendering has no host-side test net (`ui_draw.cpp` writes to the TFT framebuffer).
Compile+link catches linkage and guard mistakes; this checklist is the behavioral net for
`ui.cpp` / `ui_input.cpp` / `ui_draw.cpp`. Flash a build to the M5Stack Dial and confirm:

1. **Main screen** — gauge ring, live temperature, setpoint confirm/cancel buttons, output
   chips, page dots.
2. **Encoder** — short press (menu / confirm), long press (back / cancel), setpoint nudge on
   main, menu scroll in settings.
3. **Touch** — swipes between main and hydrometer pages; menu item taps register.
4. **Quick menu** — profile and mode shortcuts, crash gradual prompt, confirm/cancel flow.
5. **Settings menu** — open each item, edit a value, save, cancel, and reset paths behave.
6. **About / Help** — screens render without garbled or missing fonts.
7. **Output tests** — heater and pump test prompts appear; rejection toast when blocked.
8. **Wi-Fi menu item** — correct toast for network-disabled vs setup-AP-available builds.
9. **Brightness** — idle dimming works; web brightness preview updates the panel if network
   is enabled.

All nine pass → UI change is verified for this plan. Any failure is a regression until fixed or
the checklist is updated deliberately.

---

## UI follow-ups (not required for decomposition)

These are optional maintenance or future work — not gaps in the current net:

- **`ui_common.h` dedup** — `MenuIndex`, palette colors, and gauge geometry are duplicated in
  anonymous namespaces across `ui.cpp`, `ui_input.cpp`, and `ui_draw.cpp`. Extracting a shared
  header is a pure maintenance win; correctness does not depend on it.
- **Native unit tests for input logic** — `ui_input.cpp` is coupled to `M5Dial`, touch, and
  encoder APIs. A host suite would need a large M5GFX/LovyanGFX mock (or a refactor to isolate
  pure settings-delta functions). Deferred unless UI logic churn justifies the investment.
- **Render golden tests** — framebuffer snapshot comparison would require on-device capture or
  an emulator. Out of scope; manual smoke above remains the render net.
