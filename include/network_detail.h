#pragma once

#include "config.h"
#include "hydrometer.h"
#include "network.h"

#include <Arduino.h>
#include <climits>

#if FERM_ENABLE_NETWORK
#include <WebServer.h>
#endif

namespace ferm {
namespace network_detail {

extern const char *SETUP_AP_SSID_PREFIX;
extern const char *BREWFATHER_DEFAULT_URL;
constexpr uint32_t BREWFATHER_MIN_INTERVAL_SECONDS = 15UL * 60UL;
constexpr uint32_t BREWFATHER_MAX_INTERVAL_SECONDS = 24UL * 60UL * 60UL;
extern const char *FAVICON_SVG;
extern IPAddress SETUP_IP;
extern IPAddress SETUP_GATEWAY;
extern IPAddress SETUP_MASK;
extern const char *LOGIN_STYLE;
extern const char *SETUP_STYLE;

String deviceSuffix(bool uppercase);
String setupApSsid();
String networkHostname(const String &fermenterName);
String jsonFloat(float value, unsigned int decimals = 1);
String jsonIntOrNull(int32_t value, int32_t nullValue = INT32_MIN);
String hydrometerTypeText(HydrometerType type);
String hydrometerAgeText(uint32_t nowMs, uint32_t lastSeenMs);
String hydrometerDeviceName(const HydrometerReading &reading, uint8_t index);
String jsonString(const String &value);
ProfileStep parseProgramStep(const String &stepStr, bool fahrenheit);
void parseProgramSteps(ProgramSettings &program, const String &encoded,
                       bool fahrenheit);
String htmlEscape(const String &value);
String prometheusLabelEscape(const String &value);
String influxEscape(const String &value);
String sanitizeMetricBase(String value);
String urlEncode(const String &value);
String stripTrailingSlash(String value);
uint8_t runtimeStateNumber(RuntimeState state);
uint8_t userModeNumber(UserMode mode);
const char *influxTargetValue(InfluxExportTarget target);
InfluxExportTarget parseInfluxTarget(String value);
const char *exportPayloadScopeValue(ExportPayloadScope scope);
ExportPayloadScope parseExportPayloadScope(String value);
void appendField(String &fields, const String &field);
void appendJsonField(String &json, const String &field);
void appendJsonNumber(String &json, const char *name, float value,
                      unsigned int decimals);
String brewfatherDeviceState(RuntimeState state, UserMode mode,
                             bool diacetylRestActive);
String normalizeBrewfatherUrl(String value);
String normalizeBrewfatherId(String value);
String cookieValue(const String &cookieHeader, const char *name);
String mqttBaseTopic(const MqttConfig &config);

#if FERM_ENABLE_NETWORK
bool applyHydrometerSettingsFromPost(WebServer &server, Settings &settings);
bool applyEditableProfileFieldsFromPost(WebServer &server, Settings &settings,
                                        bool unitsF);
#endif

constexpr uint32_t HYDROMETER_STALE_MS = 5UL * 60UL * 1000UL;
bool hydrometerStale(const HydrometerReading &reading, uint32_t nowMs);
String hydrometerSlug(const HydrometerReading &reading, uint8_t index);

struct HaMetric {
  const char *object;
  const char *name;
  const char *valueKey;
  const char *deviceClass;
  const char *unit;
  const char *stateClass;
  const char *icon;
  bool diagnostic;
};
extern const HaMetric HA_METRICS[8];
constexpr size_t HA_METRICS_COUNT = 8;

}  // namespace network_detail
}  // namespace ferm