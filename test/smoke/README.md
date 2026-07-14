# UI smoke harness (`test/smoke/`)

Driven visual smoke for the M5Stack Dial UI. The script walks the manual
9-point checklist from `docs/test-coverage-plan.md` by injecting gestures through
`POST /api/screen/input` and capturing a framebuffer PNG per step via
`GET /api/screen`.

**What it covers:** UI logic and rendering through the screen-mirror path (same
code paths as `ui_input.cpp` / `ui_draw.cpp`, minus physical drivers).

**What it does not cover:** the physical rotary encoder and capacitive touch
panel. Run a short manual pass on those two inputs after harness review.

**Recommended build:** `m5stack_dial_demo` (Wi-Fi + screen mirror; outputs stay
simulated in demo sensor mode).

## Prerequisites

- Device on the same LAN, screen-mirror firmware flashed (`FERM_ENABLE_SCREEN_MIRROR`).
- `curl`, `python3`, `md5sum` on the host.
- Endpoints are open (no auth).

## Run

```sh
test/smoke/drive.sh <device-ip> [output_dir]
```

Example:

```sh
test/smoke/drive.sh 192.168.1.50 test/smoke/out
```

If a prior run left D-Rest active:

```sh
test/smoke/drive.sh --restore-only 192.168.1.50
```

Outputs numbered PNGs (`01_main_baseline.png`, …) under `output_dir`. Review
each frame for garbled fonts, layout breaks, or wrong colors. Live values
(temperature, SG, RSSI) vary per run — this is visual review, not byte-exact
golden comparison.

The `settle` primitive polls `/api/screen` until the framebuffer MD5 changes
(relative to the pre-gesture frame), which doubles as a cheap assertion that
each gesture had a visible effect.

## Checklist → gestures

| # | Item | Harness steps | Expected screen |
|---|------|---------------|-----------------|
| 1 | Main + setpoint confirm/cancel | `cap`; `scroll`; confirm tap; `scroll`; cancel tap | Gauge ring; gold setpoint preview; Set/Cancel row |
| 2 | Menu scroll / value nudge | `hold` → group list → Control → **Cool above** edit nudge | Group list; Control carousel; editable delta |
| 3 | Page swipes | `swipe -90 0` / `swipe 90 0` | Hydrometer page, then Main |
| 4 | Quick menu | main tap → quick; mode flow; QuickProfile → Crash → gradual prompt | Quick (Profile/Mode/D-Rest); crash gradual prompt |
| 5 | Settings edit/save/cancel | Control → **Cool above** / **Heat below** edit; save/cancel | Edit, confirm-save, group item list |
| 6 | About / Help fonts | help badge tap; System → About | Help and About screens, legible fonts |
| 7 | Output test + reject toast | Daily → Mode OFF; System → heater test confirm | Confirm-test prompt; “Test blocked” toast |
| 8 | Wi-Fi item toast | System → Wi-Fi item tap | Setup-AP or network toast (build-dependent) |
| 9 | Brightness / idle | baseline `cap` only | Main at configured brightness; **idle dim is manual** (on-dial edit: System → Brightness) |

## Primitives (`drive.sh`)

| Command | API |
|---------|-----|
| `tap x y` | `type=tap` |
| `swipe dx dy` | `type=swipe` |
| `hold` | `type=hold` |
| `scroll n` | `type=scroll&delta=n` (encoder counts; drives setpoint preview on Main) |
| `cap name` | `GET /api/screen` → `decode_screen.py` → `NN_name.png` |
| `settle` | Poll until framebuffer MD5 changes |
| `menu_goto N` | Scroll down *N* from list entry 0 (no wrap-scroll anchor) |
| `enter_group` | Centre-tap to open the focused settings group |
| `open_menu_at G I` | `hold` → group list → group *G* → item *I* (group-relative) |

`decode_screen.py` converts RGB565 big-endian raw frames to PNG (stdlib only).

**Navigation notes:** Settings are two-level (**Daily / Control / System**).
`hold` steps item-list → group-list → Main (not always straight to Main).
`menu_goto N` assumes the list is currently on entry 0 — true right after
`open_menu` / `enter_group` (firmware resets focus). Do **not** “anchor” by
scrolling up a fixed count; wrapping lists make that land on the wrong item
when the count is not a multiple of the list length. Re-enter the group to
re-anchor mid-flow. Use `open_menu_at G I` from Main. Preset **Target** is
read-only for built-in profiles; edit steps use **Cool above** / **Mode**.
Never park on Daily **D-Rest** during scroll demos — a centre tap starts the rest.
Recalling/applying a profile ends any running program and D-Rest in firmware (`activateProfile`).
The harness still runs `restore_device_state` as a belt-and-suspenders cleanup
(`dRestAction=end`, `profile=0`, `mode=AUTO` if needed).

## Residual manual pass

After reviewing harness PNGs:

1. **Physical encoder** — short press, long press, setpoint nudge on Main.
2. **Physical touch** — swipes and taps on the round panel.
3. **Idle brightness dim** — wait for idle timeout (~30 s) and confirm dimming.

## CI note

Device-bound; not suitable for CI. Lives alongside `test/golden/` as a
re-runnable local asset. Golden HTTP captures do not use `/api/screen`, so this
harness does not affect golden baselines.