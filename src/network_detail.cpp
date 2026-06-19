#include "network_detail.h"

namespace ferm {
namespace network_detail {

const char *SETUP_AP_SSID_PREFIX = "FermentDial-Setup";
const char *BREWFATHER_DEFAULT_URL =
    "https://log.brewfather.net/stream";
// The onboarding access point is open (no password) — it only exists before the
// device has joined a network, carries no secrets, and reboots away after setup.
// The web config pages also ship unlocked; users opt into a password under
// Settings > Security.

// On-brand favicon: a thermostat dial gauge in the dashboard palette. Served as
// SVG so it scales crisply and costs almost nothing in flash.
const char *FAVICON_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 32 32\">"
    "<rect width=\"32\" height=\"32\" rx=\"7\" fill=\"#0d1b1e\"/>"
    "<path d=\"M8.2 23 A10 10 0 1 1 23.8 23\" fill=\"none\" stroke=\"#1e3840\" "
    "stroke-width=\"3\" stroke-linecap=\"round\"/>"
    "<path d=\"M8.2 23 A10 10 0 0 1 16 6\" fill=\"none\" stroke=\"#36c87a\" "
    "stroke-width=\"3\" stroke-linecap=\"round\"/>"
    "<line x1=\"16\" y1=\"16\" x2=\"21\" y2=\"10.5\" stroke=\"#b0d8f8\" "
    "stroke-width=\"2.4\" stroke-linecap=\"round\"/>"
    "<circle cx=\"16\" cy=\"16\" r=\"2.6\" fill=\"#b0d8f8\"/></svg>";

IPAddress SETUP_IP(192, 168, 4, 1);
IPAddress SETUP_GATEWAY(192, 168, 4, 1);
IPAddress SETUP_MASK(255, 255, 255, 0);

String deviceSuffix(bool uppercase) {
  char suffix[5];
  snprintf(suffix, sizeof(suffix), uppercase ? "%04X" : "%04x",
           static_cast<uint16_t>(ESP.getEfuseMac() & 0xFFFF));
  return String(suffix);
}

String setupApSsid() {
  const String suffix = deviceSuffix(true);
  return String(SETUP_AP_SSID_PREFIX) + "-" + suffix;
}

String hostnameToken(String value) {
  value.trim();
  value.toLowerCase();

  String token = "";
  token.reserve(value.length());
  bool pendingHyphen = false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value.charAt(i);
    const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (alnum) {
      if (pendingHyphen && token.length() > 0) {
        token += '-';
      }
      token += c;
      pendingHyphen = false;
    } else {
      pendingHyphen = token.length() > 0;
    }
  }
  return token;
}

String networkHostname(const String &fermenterName) {
  String hostname = hostnameToken(FERM_WIFI_HOSTNAME_BASE);
  if (hostname.length() == 0) {
    hostname = "fermentdial";
  }

  const String name = hostnameToken(fermenterName);
  const String defaultName = hostnameToken(DEFAULT_FERMENTER_NAME);
  const String suffix = deviceSuffix(false);

  if (name.length() > 0 && name != defaultName) {
    hostname += "-";
    hostname += name;
  }

  const size_t maxPrefixLength = 63 - 1 - suffix.length();
  if (hostname.length() > maxPrefixLength) {
    hostname = hostname.substring(0, maxPrefixLength);
    while (hostname.endsWith("-")) {
      hostname.remove(hostname.length() - 1);
    }
  }

  hostname += "-";
  hostname += suffix;
  return hostname;
}

String jsonFloat(float value, unsigned int decimals) {
  return isnan(value) ? "null" : String(value, decimals);
}

String jsonIntOrNull(int32_t value, int32_t nullValue) {
  return value == nullValue ? "null" : String(value);
}

String hydrometerTypeText(HydrometerType type) {
  switch (type) {
  case HydrometerType::Tilt:
    return "TILT";
  case HydrometerType::Rapt:
    return "RAPT";
  default:
    return "UNKNOWN";
  }
}

String hydrometerAgeText(uint32_t nowMs, uint32_t lastSeenMs) {
  if (lastSeenMs == 0) {
    return "null";
  }
  return String((nowMs - lastSeenMs) / 1000UL);
}

String hydrometerDeviceName(const HydrometerReading &reading, uint8_t index) {
  if (reading.label.length() > 0) {
    return reading.label;
  }
  if (reading.name.length() > 0) {
    return reading.name;
  }
  if (reading.color.length() > 0) {
    return hydrometerTypeText(reading.type) + " " + reading.color;
  }
  return String("Hydrometer ") + String(index + 1);
}

String jsonString(const String &value) {
  String escaped = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '"' || c == '\\') {
      escaped += '\\';
    }
    if (c >= 0x20) {
      escaped += c;
    }
  }
  escaped += "\"";
  return escaped;
}

// Parse one encoded step: "type,exit,targetDisplay,durationSeconds,gravity,
// stableHours". Missing fields default to 0; out-of-range enums are clamped.
ProfileStep parseProgramStep(const String &stepStr, bool fahrenheit) {
  ProfileStep step;
  String parts[6];
  uint8_t count = 0;
  int fieldStart = 0;
  while (count < 6) {
    int comma = stepStr.indexOf(',', fieldStart);
    if (comma < 0) {
      parts[count++] = stepStr.substring(fieldStart);
      break;
    }
    parts[count++] = stepStr.substring(fieldStart, comma);
    fieldStart = comma + 1;
  }
  long typeVal = parts[0].toInt();
  if (typeVal < 0 || typeVal > static_cast<long>(StepType::ManualWait)) {
    typeVal = static_cast<long>(StepType::Hold);
  }
  step.type = static_cast<StepType>(typeVal);
  long exitVal = parts[1].toInt();
  if (exitVal < 0 || exitVal > static_cast<long>(StepExit::Manual)) {
    exitVal = static_cast<long>(StepExit::Time);
  }
  step.exit = static_cast<StepExit>(exitVal);
  step.targetC = fromDisplayTemp(parts[2].toFloat(), fahrenheit);
  float dur = parts[3].toFloat();
  step.durationSeconds = dur > 0 ? static_cast<uint32_t>(dur) : 0;
  step.gravityThreshold = parts[4].toFloat();
  long stable = parts[5].toInt();
  if (stable < 0) {
    stable = 0;
  } else if (stable > 65535) {
    stable = 65535;
  }
  step.stableHours = static_cast<uint16_t>(stable);
  return step;
}

// Parse a program's steps from "step;step;..." into the program slot.
void parseProgramSteps(ProgramSettings &program, const String &encoded,
                       bool fahrenheit) {
  program.stepCount = 0;
  const int len = encoded.length();
  int start = 0;
  while (start < len && program.stepCount < MAX_PROGRAM_STEPS) {
    int semi = encoded.indexOf(';', start);
    String stepStr =
        (semi < 0) ? encoded.substring(start) : encoded.substring(start, semi);
    stepStr.trim();
    if (stepStr.length() > 0) {
      program.steps[program.stepCount++] = parseProgramStep(stepStr, fahrenheit);
    }
    if (semi < 0) {
      break;
    }
    start = semi + 1;
  }
}

String htmlEscape(const String &value) {
  String escaped = "";
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '&') {
      escaped += F("&amp;");
    } else if (c == '<') {
      escaped += F("&lt;");
    } else if (c == '>') {
      escaped += F("&gt;");
    } else if (c == '"') {
      escaped += F("&quot;");
    } else if (c >= 0x20) {
      escaped += c;
    }
  }
  return escaped;
}

String prometheusLabelEscape(const String &value) {
  String escaped = "";
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '\\' || c == '"') {
      escaped += '\\';
      escaped += c;
    } else if (c == '\n') {
      escaped += F("\\n");
    } else if (c >= 0x20) {
      escaped += c;
    }
  }
  return escaped;
}

String influxEscape(const String &value) {
  String escaped = "";
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == ' ' || c == ',' || c == '=' || c == '\\') {
      escaped += '\\';
    }
    if (c >= 0x20) {
      escaped += c;
    }
  }
  return escaped;
}

bool isUrlSafe(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
         c == '~';
}

// Sanitize a user-supplied base name so it is valid as both an InfluxDB
// measurement and a Prometheus metric-name prefix. Invalid characters become
// '_', empty falls back to "fermentdial", and a leading digit is prefixed so
// the result is a legal Prometheus identifier.
String sanitizeMetricBase(String value) {
  value.trim();
  String out = "";
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_';
    out += ok ? c : '_';
  }
  if (out.length() == 0) {
    return "fermentdial";
  }
  if (out.charAt(0) >= '0' && out.charAt(0) <= '9') {
    out = "_" + out;
  }
  return out;
}

String urlEncode(const String &value) {
  const char *hex = "0123456789ABCDEF";
  String encoded = "";
  encoded.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = static_cast<uint8_t>(value.charAt(i));
    if (isUrlSafe(static_cast<char>(c))) {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

String stripTrailingSlash(String value) {
  value.trim();
  while (value.endsWith("/")) {
    value.remove(value.length() - 1);
  }
  return value;
}

uint8_t runtimeStateNumber(RuntimeState state) {
  return static_cast<uint8_t>(state);
}

uint8_t userModeNumber(UserMode mode) {
  return static_cast<uint8_t>(mode);
}

const char *influxTargetValue(InfluxExportTarget target) {
  switch (target) {
  case InfluxExportTarget::V2:
    return "2";
  case InfluxExportTarget::V3:
    return "3";
  case InfluxExportTarget::VictoriaMetrics:
    return "vm";
  case InfluxExportTarget::V1:
  default:
    return "1";
  }
}

InfluxExportTarget parseInfluxTarget(String value) {
  value.trim();
  value.toLowerCase();
  if (value == "2" || value == "v2") {
    return InfluxExportTarget::V2;
  }
  if (value == "3" || value == "v3") {
    return InfluxExportTarget::V3;
  }
  if (value == "vm" || value == "victoriametrics") {
    return InfluxExportTarget::VictoriaMetrics;
  }
  return InfluxExportTarget::V1;
}

const char *exportPayloadScopeValue(ExportPayloadScope scope) {
  switch (scope) {
  case ExportPayloadScope::HydrometerOnly:
    return "hydrometer";
  case ExportPayloadScope::ControllerAndHydrometer:
  default:
    return "all";
  }
}

ExportPayloadScope parseExportPayloadScope(String value) {
  value.trim();
  value.toLowerCase();
  if (value == "hydrometer" || value == "hydrometer_only" ||
      value == "hydro") {
    return ExportPayloadScope::HydrometerOnly;
  }
  return ExportPayloadScope::ControllerAndHydrometer;
}

void appendField(String &fields, const String &field) {
  if (fields.length() > 0) {
    fields += ",";
  }
  fields += field;
}

void appendJsonField(String &json, const String &field) {
  if (!json.endsWith("{")) {
    json += ",";
  }
  json += field;
}

void appendJsonNumber(String &json, const char *name, float value,
                      unsigned int decimals) {
  appendJsonField(json, jsonString(String(name)) + ":" +
                            String(value, decimals));
}

String brewfatherDeviceState(RuntimeState state, UserMode mode,
                             bool diacetylRestActive) {
  if (mode == UserMode::Off || state == RuntimeState::Off) {
    return "off";
  }
  if (diacetylRestActive) {
    return "on";
  }
  switch (state) {
  case RuntimeState::Heating:
    return "heating";
  case RuntimeState::Cooling:
    return "cooling";
  case RuntimeState::Fault:
    return "fault";
  case RuntimeState::Idle:
    return "on";
  case RuntimeState::Boot:
  default:
    return "off";
  }
}

String normalizeBrewfatherUrl(String value) {
  value.trim();
  if (value.length() == 0) {
    return BREWFATHER_DEFAULT_URL;
  }
  return value;
}

String normalizeBrewfatherId(String value) {
  value.trim();
  const int idIndex = value.indexOf("id=");
  if (idIndex >= 0) {
    int start = idIndex + 3;
    int end = value.indexOf('&', start);
    if (end < 0) {
      end = value.length();
    }
    value = value.substring(start, end);
    value.trim();
  }
  return value;
}

// Pull a single cookie value out of a raw Cookie header ("a=1; b=2").
String cookieValue(const String &cookieHeader, const char *name) {
  const String key = String(name) + "=";
  int idx = cookieHeader.indexOf(key);
  if (idx < 0) {
    return "";
  }
  idx += key.length();
  int end = cookieHeader.indexOf(';', idx);
  if (end < 0) {
    end = cookieHeader.length();
  }
  String value = cookieHeader.substring(idx, end);
  value.trim();
  return value;
}


String mqttBaseTopic(const MqttConfig &config) {
  String base = config.baseTopic;
  base.trim();
  if (base.length() == 0) {
    base = "fermentdial";
  }
  while (base.endsWith("/")) {
    base = base.substring(0, base.length() - 1);
  }
  return base;
}

#if FERM_ENABLE_NETWORK

bool applyHydrometerSettingsFromPost(WebServer &server, Settings &settings) {
  bool changed = false;

  if (server.hasArg("hydrometerBleEnabled")) {
    setHydrometerScanEnabled(settings,
                             server.arg("hydrometerBleEnabled").toInt() != 0);
    changed = true;
  }

  if (server.hasArg("hydrometerScanType")) {
    HydrometerScanType scanType = HydrometerScanType::Unknown;
    if (parseHydrometerScanTypeArg(server.arg("hydrometerScanType"), scanType)) {
      setHydrometerScanTypeFromUi(settings, scanType);
      changed = true;
    }
  }

  if (server.hasArg("hydrometerSelectKey")) {
    String selectedKey = server.arg("hydrometerSelectKey");
    selectedKey.trim();
    if (selectedKey != settings.hydrometerSelectionKey) {
      selectHydrometerDevice(settings, selectedKey);
      changed = true;
    }
  }

  if (server.hasArg("hydrometerClearSelection")) {
    if (settings.hydrometerSelectionKey.length() > 0) {
      clearHydrometerSelection(settings);
      changed = true;
    }
  }

  if (server.hasArg("hydrometerResetOg")) {
    resetHydrometerSession(settings);
    changed = true;
  }

  return changed;
}

bool applyEditableProfileFieldsFromPost(WebServer &server, Settings &settings,
                                        bool unitsF) {
  bool changed = false;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    if (!profileSlotEditable(i)) {
      continue;
    }
    const String nameArg = String("profile") + String(i) + "Name";
    const String targetArg = String("profile") + String(i) + "Target";
    if (server.hasArg(nameArg)) {
      settings.profiles[i].name = server.arg(nameArg);
      changed = true;
    }
    if (server.hasArg(targetArg)) {
      settings.profiles[i].targetC =
          fromDisplayTemp(server.arg(targetArg).toFloat(), unitsF);
      changed = true;
    }
  }
  return changed;
}

#endif  // FERM_ENABLE_NETWORK

bool hydrometerStale(const HydrometerReading &reading, uint32_t nowMs) {
  return reading.lastSeenMs == 0 ||
         (nowMs - reading.lastSeenMs) > HYDROMETER_STALE_MS;
}

// Turn a hydrometer key ("tilt:red", "rapt:aa:bb:cc..") into an MQTT- and
// HA-safe slug ("tilt_red", "rapt_aabbcc..").
String hydrometerSlug(const HydrometerReading &reading, uint8_t index) {
  const String src =
      reading.key.length() > 0 ? reading.key : String("hydro_") + String(index);
  String slug;
  slug.reserve(src.length());
  for (size_t i = 0; i < src.length(); ++i) {
    const char c = src.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
      slug += c;
    } else if (c >= 'A' && c <= 'Z') {
      slug += static_cast<char>(c - 'A' + 'a');
    } else {
      slug += '_';
    }
  }
  return slug;
}

const HaMetric HA_METRICS[] = {
    {"gravity", "Gravity", "gravity", nullptr, "SG", "measurement", "mdi:water",
     false},
    {"temperature", "Temperature", "temp", "temperature", "°C",
     "measurement", nullptr, false},
    {"abv", "ABV", "abv", nullptr, "%", "measurement", "mdi:glass-mug-variant",
     false},
    {"velocity", "Gravity velocity", "velocity", nullptr, "SG/day", nullptr,
     "mdi:speedometer", false},
    {"stable", "Stable time", "stable_s", "duration", "s", nullptr, nullptr,
     false},
    {"battery", "Battery", "battery", "voltage", "V", "measurement", nullptr,
     true},
    {"rssi", "Signal", "rssi", "signal_strength", "dBm", "measurement", nullptr,
     true},
};
const char *LOGIN_STYLE = R"HTML(<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:420px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
h1{margin-top:0;color:#b0d8f8}input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}.err{color:#ffb4a8;font-weight:700}a{color:#79d4ff}
</style>)HTML";

const char *SETUP_STYLE = R"HTML(<style>
html{background:#071015}body{font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:#071015;color:#f8fbff;margin:0;min-height:100vh}
main{max-width:480px;margin:auto;padding:24px}.card{border-radius:8px;background:#132428;border:1px solid #1e3840;padding:22px}
input,button{font:inherit;width:100%;box-sizing:border-box;margin:8px 0 16px;padding:14px;border-radius:8px;border:1px solid #1e3840}
input{background:#102126;color:#d0e8f0}button{background:#356f89;color:white;font-weight:900}.hint{color:#a9bac8}a{color:#79d4ff}
.scanStatus{min-height:20px;margin:0 0 8px}.networkList{display:grid;gap:6px;margin:0 0 16px}.networkList button{display:flex;justify-content:space-between;gap:10px;background:#102126;color:#f8fbff;font-weight:700;text-align:left;margin:0}.networkMeta{color:#a9bac8;font-size:12px;white-space:nowrap}
</style>)HTML";

}  // namespace network_detail
}  // namespace ferm
