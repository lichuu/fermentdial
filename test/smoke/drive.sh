#!/usr/bin/env bash
# Driven UI smoke harness — walks the 9-point checklist via /api/screen/input.
#
#   test/smoke/drive.sh 192.168.30.48 test/smoke/out
#
# Requires a screen-mirror build (m5stack_dial_demo or m5stack_dial_wifi).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

RESTORE_ONLY=0
if [[ "${1:-}" == "--restore-only" ]]; then
  RESTORE_ONLY=1
  HOST="${2:?Usage: $0 --restore-only <host>}"
  OUT_DIR="${SCRIPT_DIR}/.restore-scratch"
else
  HOST="${1:?Usage: $0 <host> [output_dir] or $0 --restore-only <host>}"
  OUT_DIR="${2:-$(dirname "$0")/out-$(date +%Y%m%d-%H%M%S)}"
fi
BASE="http://${HOST}"
CURL_OPTS=(-sS --fail-with-body)
RAW_FRAME="$OUT_DIR/.frame.bin"
HDR_FILE="$OUT_DIR/.frame.hdr"

# 240x240 round display — centre and menu zone taps.
readonly CX=120
readonly CY=120
readonly MENU_UP_Y=40
readonly MENU_DOWN_Y=200
readonly SETPOINT_CONFIRM_X=168
readonly SETPOINT_CANCEL_X=72
readonly SETPOINT_ROW_Y=177
# Help badge hit zone (ui_input.cpp setpoint carve-out): x∈[22,50], y∈[106,134].
readonly HELP_X=36
readonly HELP_Y=120
# Menu indices (ui_input.cpp MenuIndex) — use menu_goto, not relative scroll from drift.
readonly MENU_PROFILE=0
readonly MENU_TARGET=1
readonly MENU_DREST=2
readonly MENU_MODE=3
readonly MENU_COOL_ON=4
readonly MENU_HEAT_ON=5
readonly MENU_WIFI=11
readonly MENU_HEATER_TEST=14
readonly MENU_ABOUT=16
readonly PROFILE_CRASH=4
readonly PROFILE_ALE=0
readonly PROFILE_COUNT=7
readonly EDIT_NUDGE_LEFT_X=60
readonly EDIT_NUDGE_RIGHT_X=180
readonly EDIT_VALUE_Y=100
readonly EDIT_SAVE_X=180
readonly EDIT_ROW_Y=165
readonly EDIT_BACK_Y=186
readonly QUICK_CANCEL_X=72
readonly CONFIRM_ROW_Y=177
# quickCancelHit row on QuickMenu / QuickProfile / QuickMode (not centre).
readonly QUICK_MENU_CANCEL_Y=174

mkdir -p "$OUT_DIR"
CAP_SEQ=1
# Poll budget for settle(); each try is a full 115 KB GET on the device loop.
SETTLE_MAX=15
# One encoder step via remote scroll (= MENU/EDIT_ENCODER_DIVISOR in ui_input.cpp).
readonly SCROLL_STEP=2

trap 'rm -f "$RAW_FRAME" "$HDR_FILE"' EXIT

log() {
  printf '[smoke] %s\n' "$*"
}

frame_md5() {
  curl "${CURL_OPTS[@]}" -o "$RAW_FRAME" "$BASE/api/screen"
  md5sum "$RAW_FRAME" | awk '{print $1}'
}

send_input() {
  curl "${CURL_OPTS[@]}" -X POST "$BASE/api/screen/input" "$@" -o /dev/null
}

tap() {
  send_input -d "type=tap&x=${1}&y=${2}"
}

swipe() {
  send_input -d "type=swipe&dx=${1}&dy=${2}"
}

hold() {
  send_input -d "type=hold"
}

scroll() {
  send_input -d "type=scroll&delta=${1}"
}

scroll_settle() {
  local delta="$1"
  local before
  before="$(frame_md5)"
  scroll "$delta"
  settle "$before" || true
}

cap() {
  local name="$1"
  local idx
  idx="$(printf '%02d' "$CAP_SEQ")"
  CAP_SEQ=$((CAP_SEQ + 1))

  curl "${CURL_OPTS[@]}" -D "$HDR_FILE" -o "$RAW_FRAME" "$BASE/api/screen"
  local w h stride
  w="$(grep -i '^x-frame-width:' "$HDR_FILE" | awk '{print $2}' | tr -d '\r')"
  h="$(grep -i '^x-frame-height:' "$HDR_FILE" | awk '{print $2}' | tr -d '\r')"
  stride="$(grep -i '^x-frame-stride:' "$HDR_FILE" | awk '{print $2}' | tr -d '\r')"
  if [[ -z "$w" || -z "$h" || -z "$stride" ]]; then
    echo "error: missing X-Frame-* headers from /api/screen" >&2
    exit 1
  fi

  local png="$OUT_DIR/${idx}_${name}.png"
  python3 "$SCRIPT_DIR/decode_screen.py" "$RAW_FRAME" "$w" "$h" "$stride" "$png"
  log "captured ${png}"
}

settle() {
  local baseline="$1"
  local max_tries="${2:-$SETTLE_MAX}"
  local i md5
  for ((i = 0; i < max_tries; i++)); do
    md5="$(frame_md5)"
    if [[ "$md5" != "$baseline" ]]; then
      log "settle: frame changed after $((i + 1)) poll(s)"
      return 0
    fi
  done
  log "settle: warning — no frame change after ${max_tries} poll(s)"
  return 1
}

step() {
  local name="$1"
  shift
  log "STEP $(printf '%02d' "$CAP_SEQ"): ${name}"
  # A bare `true` action is a "just capture this state" marker (e.g. a post-
  # go_main baseline). There is no gesture, so skip settle — it would always
  # warn about an expected no-op and drown out the real signal.
  if [[ "$#" -eq 1 && "$1" == "true" ]]; then
    cap "$name"
    return
  fi
  local before
  before="$(frame_md5)"
  "$@"
  settle "$before" || true
  cap "$name"
}

menu_scroll_down() {
  local n="${1:-1}"
  local i before
  for ((i = 0; i < n; i++)); do
    before="$(frame_md5)"
    tap "$CX" "$MENU_DOWN_Y"
    settle "$before" || true
  done
}

menu_scroll_up() {
  local n="${1:-1}"
  local i before
  for ((i = 0; i < n; i++)); do
    before="$(frame_md5)"
    tap "$CX" "$MENU_UP_Y"
    settle "$before" || true
  done
}

# _menuIndex persists across menu open/close — anchor to Profile (0) before absolute jumps.
menu_anchor() {
  menu_scroll_up 20
}

menu_goto() {
  local index="${1:?menu index}"
  menu_anchor
  menu_scroll_down "$index"
}

quick_scroll_down() {
  local n="${1:-1}"
  local i before
  for ((i = 0; i < n; i++)); do
    before="$(frame_md5)"
    tap "$CX" "$MENU_DOWN_Y"
    settle "$before" || true
  done
}

# Enter Menu from Main/Hydrometer only — hold from Menu escapes to Main.
open_menu() {
  local before
  before="$(frame_md5)"
  hold
  settle "$before" || true
}

open_menu_at() {
  open_menu
  menu_goto "$1"
}

status_field() {
  local expr="$1"
  curl "${CURL_OPTS[@]}" "$BASE/api/status" |
    python3 -c "import json,sys; d=json.load(sys.stdin); print(${expr})"
}

drest_active() {
  status_field "d['diacetylRest']['active']"
}

active_profile_index() {
  status_field "d['activeProfile']"
}

device_mode() {
  status_field "d['mode']"
}

settings_post() {
  curl "${CURL_OPTS[@]}" -X POST -d "$1" "$BASE/api/settings" -o /dev/null
}

# End-of-run cleanup — POST /api/settings is open (no auth) and reliable; UI menu
# taps to finish D-Rest are brittle when navigation has drifted.
restore_device_state() {
  if [[ "$(drest_active)" == "True" ]]; then
    log "restore: dRestAction=end"
    settings_post "dRestAction=end"
  fi
  local prof
  prof="$(active_profile_index)"
  if [[ "$prof" != "$PROFILE_ALE" ]]; then
    log "restore: profile=${PROFILE_ALE} (was ${prof})"
    settings_post "profile=${PROFILE_ALE}"
  fi
  if [[ "$(device_mode)" == "OFF" ]]; then
    log "restore: mode=AUTO"
    settings_post "mode=AUTO"
  fi
  go_main
}

go_main() {
  local i before
  # Never tap screen centre here — on Main that opens Quick menu (ui_input.cpp).
  # Dismiss overlays, route through Menu (hold), then swipe left to Main (page 0).
  for ((i = 0; i < 3; i++)); do
    before="$(frame_md5)"
    tap "$QUICK_CANCEL_X" "$CONFIRM_ROW_Y"
    settle "$before" || true
    before="$(frame_md5)"
    tap "$CX" "$QUICK_MENU_CANCEL_Y"
    settle "$before" || true
    before="$(frame_md5)"
    hold
    settle "$before" || true
    before="$(frame_md5)"
    swipe -90 0
    settle "$before" || true
  done
}

snap() {
  local name="$1"
  log "SNAP $(printf '%02d' "$CAP_SEQ"): ${name}"
  cap "$name"
}

run_checklist() {
  log "output directory: ${OUT_DIR}"
  log "starting checklist (device should be on Main; recovering if not)"
  go_main
  step "main_baseline" true

  # 1 — Main + setpoint confirm/cancel
  step "setpoint_preview" scroll "$SCROLL_STEP"
  step "setpoint_confirm" tap "$SETPOINT_CONFIRM_X" "$SETPOINT_ROW_Y"
  step "setpoint_preview_again" scroll "$SCROLL_STEP"
  step "setpoint_cancel" tap "$SETPOINT_CANCEL_X" "$SETPOINT_ROW_Y"

  # 2 — Menu scroll / value nudge (Cool on — editable; never stop on D-Rest — tap starts it)
  step "menu_open" open_menu
  menu_goto "$MENU_COOL_ON"
  snap "menu_cool_on"
  menu_scroll_down 1
  snap "menu_scrolled_down"
  menu_scroll_up 1
  snap "menu_scrolled_up"
  menu_goto "$MENU_COOL_ON"
  step "edit_open" tap "$CX" "$CY"
  step "edit_nudge" tap "$EDIT_NUDGE_RIGHT_X" "$EDIT_VALUE_Y"
  step "edit_back" tap "$CX" "$EDIT_BACK_Y"
  go_main
  step "main_from_menu" true

  # 3 — Page swipes (main <-> hydrometer)
  step "hydrometer" swipe -90 0
  step "main_swipe_back" swipe 90 0

  # 4 — Quick menu (profile/mode/crash)
  step "quick_menu" tap "$CX" "$CY"
  step "quick_mode_action" tap "$CX" "$MENU_DOWN_Y"
  step "quick_mode_picker" tap "$CX" "$CY"
  step "quick_confirm" tap "$CX" "$CY"
  step "quick_cancel" tap "$QUICK_CANCEL_X" "$CONFIRM_ROW_Y"

  step "quick_menu_crash" tap "$CX" "$CY"
  step "quick_profile_picker" tap "$CX" "$CY"
  quick_scroll_down "$PROFILE_CRASH"
  snap "quick_crash_profile"
  step "crash_gradual_prompt" tap "$CX" "$CY"
  step "crash_cancel" tap "$QUICK_CANCEL_X" "$CONFIRM_ROW_Y"
  go_main
  step "main_after_quick" true

  # 5 — Settings open/edit/save/cancel (Cool on / Heat on — both editable deltas)
  open_menu_at "$MENU_COOL_ON"
  step "settings_edit" tap "$CX" "$CY"
  step "settings_nudge" tap "$EDIT_NUDGE_RIGHT_X" "$EDIT_VALUE_Y"
  step "settings_save_prompt" tap "$EDIT_SAVE_X" "$EDIT_ROW_Y"
  step "settings_save_confirm" tap "$SETPOINT_CONFIRM_X" "$CONFIRM_ROW_Y"
  # saveEdit returns to Menu — do not hold here (hold from Menu escapes to Main).
  menu_goto "$MENU_HEAT_ON"
  step "settings_edit_cancel" tap "$CX" "$CY"
  step "settings_cancel_back" tap "$CX" "$EDIT_BACK_Y"
  go_main
  step "main_after_settings" true

  # 6 — About / Help fonts
  go_main
  step "help" tap "$HELP_X" "$HELP_Y"
  step "help_back" tap "$CX" "$CY"
  open_menu_at "$MENU_ABOUT"
  step "about_screen" tap "$CX" "$CY"
  step "about_back" tap "$CX" "$CY"
  go_main
  step "main_after_about" true

  # 7 — Output test prompts + reject toast (mode OFF blocks the test)
  open_menu_at "$MENU_MODE"
  step "mode_edit" tap "$CX" "$CY"
  step "mode_off_nudge" scroll_settle "-${SCROLL_STEP}"
  step "mode_off_save_prompt" tap "$EDIT_SAVE_X" "$EDIT_ROW_Y"
  step "mode_off_save_confirm" tap "$SETPOINT_CONFIRM_X" "$CONFIRM_ROW_Y"
  menu_goto "$MENU_HEATER_TEST"
  step "heater_confirm_prompt" tap "$CX" "$CY"
  step "heater_reject_toast" tap "$CX" "$CY"

  # Restore Auto mode via the API. The UI mode-edit flow is already exercised by
  # the OFF path above; POST /api/settings is reliable where drifting menu taps
  # are not (same rationale as restore_device_state).
  settings_post "mode=AUTO"
  go_main
  snap "main_after_mode_restore"

  # 8 — Wi-Fi item toast
  open_menu
  menu_goto "$MENU_WIFI"
  step "wifi_toast" tap "$CX" "$CY"
  go_main
  step "main_after_wifi" true

  # 9 — Brightness baseline (idle dim is timing-based; see README)
  step "brightness_baseline" true

  restore_device_state
  step "final_main" true
  log "checklist complete — review PNGs in ${OUT_DIR}"
}

# Preflight: screen mirror must be available.
code="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/screen" || true)"
if [[ "$code" != "200" ]]; then
  echo "error: GET /api/screen returned HTTP ${code} (need FERM_ENABLE_SCREEN_MIRROR build)" >&2
  exit 1
fi

if [[ "$RESTORE_ONLY" == 1 ]]; then
  restore_device_state
  log "restore complete — drest=$(drest_active) profile=$(active_profile_index)"
  exit 0
fi

run_checklist