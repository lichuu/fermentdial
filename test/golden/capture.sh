#!/usr/bin/env bash
# Golden-master capture for HTTP/JSON regressions.
#
# Personal Wi-Fi is not part of the baseline: JSON masks drop SSID/IP and
# connection state. You do not need to provision home Wi-Fi first — after a
# factory reset, join the device setup AP and capture from 192.168.4.1:
#
#   test/golden/capture.sh 192.168.4.1 test/golden/before
#
# In setup mode "/" is the Wi-Fi provisioning page (stock post-reset shell).
# Reconnect home Wi-Fi only when you want to use the device, not for goldens.
#
# Masking notes:
# - HTML strips ?v=<git-sha> cache-busters (and status nulls firmwareGitSha),
#   so build-stamped volatiles only show up in cross-commit before/after diffs,
#   not in a same-build self-check (before vs before2).
# - Stripping ?v= reduces golden coverage of the asset-versioning query path.
set -euo pipefail

HOST="${1:?Usage: $0 <host> [output_dir]}"
OUT_DIR="${2:-$(dirname "$0")/capture-$(date +%Y%m%d-%H%M%S)}"
BASE="http://${HOST}"
COOKIE_JAR="$(mktemp)"
CURL_OPTS=(-sS --fail-with-body)

mkdir -p "$OUT_DIR"
trap 'rm -f "$COOKIE_JAR"' EXIT

log() {
  printf '[capture] %s\n' "$*"
}

needs_auth() {
  local code
  code="$(curl -s -o /dev/null -w '%{http_code}' "$BASE/api/program" || true)"
  [[ "$code" != "200" ]]
}

login_if_needed() {
  if ! needs_auth; then
    log "auth not required"
    return 0
  fi

  if [[ -z "${FERM_ADMIN_PASSWORD:-}" ]]; then
    echo "error: /api/program requires auth; set FERM_ADMIN_PASSWORD" >&2
    exit 1
  fi

  log "logging in for auth-gated endpoints"
  curl "${CURL_OPTS[@]}" -c "$COOKIE_JAR" -b "$COOKIE_JAR" \
    -X POST "$BASE/login" \
    -d "password=${FERM_ADMIN_PASSWORD}" \
    -o /dev/null
}

auth_curl() {
  if [[ -s "$COOKIE_JAR" ]]; then
    curl "${CURL_OPTS[@]}" -b "$COOKIE_JAR" "$@"
  else
    curl "${CURL_OPTS[@]}" "$@"
  fi
}

# Drop ?v=<sha> so HTML goldens survive rebuilds; see header masking notes.
normalize_html() {
  tr -d '\r' | sed -E 's/[?&]v=[a-f0-9]+//g'
}

normalize_program_json() {
  jq -S .
}

normalize_settings_json() {
  jq -S 'del(.wifiSsid, .wifiIp, .apSsid, .hostname)
         | .influx.lastStatus = null
         | .mqtt.lastStatus = null
         | .brewfather.lastStatus = null'
}

normalize_status_json() {
  jq -S '
    .uptimeSeconds = null
    | .clock = null
    | .temperature = null
    | .target = null
    | .ip = null
    | .hostname = null
    | .firmwareGitSha = null
    | .wifiConnected = null
    | .wifiStatus = null
    | .program.stepElapsedSeconds = null
    | .program.stepRemainingSeconds = null
    | .diacetylRest.remainingSeconds = null
    | .diacetylRest.remainingHours = null
    | .hydrometer.rssi = null
    | .hydrometer.temperature = null
    | .hydrometer.gravity = null
    | .hydrometer.gravityVelocity = null
    | .hydrometer.stableSeconds = null
    | .hydrometer.lastSeenSeconds = null
    | .hydrometer.batteryV = null
    | .hydrometer.abv = null
    | .hydrometerDevices = (
        .hydrometerDevices
        | map(
            .rssi = null
            | .temperature = null
            | .gravity = null
            | .batteryV = null
            | .lastSeenSeconds = null
          )
      )
  '
}

normalize_metrics() {
  grep -v '^#' | sed -E 's/[[:space:]]+[-+0-9.eE]+$/ /' \
    | awk 'NF { sub(/[[:space:]]+$/, ""); print }' \
    | sort
}

capture_endpoint() {
  local name="$1"
  local path="$2"
  local auth="$3"
  local normalizer="$4"
  local outfile="$OUT_DIR/${name}.txt"

  log "capturing ${path}"
  if [[ "$auth" == "auth" ]]; then
    auth_curl "$BASE${path}" | eval "$normalizer" >"$outfile"
  else
    curl "${CURL_OPTS[@]}" "$BASE${path}" | eval "$normalizer" >"$outfile"
  fi
}

skip_endpoint() {
  log "skipping ${1} (${2})"
  printf 'SKIPPED: %s\n' "$2" >"$OUT_DIR/${1}.txt"
}

login_if_needed

capture_endpoint "root" "/" "open" "normalize_html"
capture_endpoint "settings" "/settings" "auth" "normalize_html"
capture_endpoint "firmware" "/firmware" "auth" "normalize_html"
capture_endpoint "login" "/login" "open" "normalize_html"
capture_endpoint "api_program" "/api/program" "auth" "normalize_program_json"
capture_endpoint "api_settings" "/api/settings" "auth" "normalize_settings_json"
capture_endpoint "api_status" "/api/status" "open" "normalize_status_json"
capture_endpoint "metrics" "/metrics" "open" "normalize_metrics"

skip_endpoint "api_history" "volatile history payload"
skip_endpoint "api_history_csv" "volatile CSV export"
skip_endpoint "api_events" "volatile event log"
skip_endpoint "api_selfcheck" "volatile self-check details"

log "wrote golden files to ${OUT_DIR}"