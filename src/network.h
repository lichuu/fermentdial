#pragma once

#include "config.h"
#include "control.h"
#include "events.h"
#include "hydrometer.h"

#if FERM_ENABLE_NETWORK
#include <DNSServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFiClient.h>
#endif

namespace ferm {

struct NetworkSnapshot {
  bool wifiConnected = false;
  bool mqttConnected = false;
  bool mqttEnabled = false;
  bool otaEnabled = false;
  bool wifiEnabled = false;
  bool wifiConfigured = false;
  String ipAddress = "";
  String hostname = "";
  String ssid = "";
  String status = "WiFi OFF";
};

struct WebStatus {
  bool tempValid = false;
  float tempC = NAN;
  String fermenterName = DEFAULT_FERMENTER_NAME;
  ProfileSettings profiles[PROFILE_COUNT];
  uint8_t activeProfile = static_cast<uint8_t>(ProfileSlot::Ale);
  float liveTargetC = DEFAULT_TARGET_C;
  bool diacetylRestActive = false;
  float diacetylRestTargetC = DEFAULT_DIACETYL_REST_TARGET_C;
  uint32_t diacetylRestDurationSeconds = DEFAULT_DIACETYL_REST_DURATION_SECONDS;
  uint32_t diacetylRestRemainingSeconds = 0;
  uint8_t diacetylRestReturnProfile =
      static_cast<uint8_t>(ProfileSlot::Ale);
  float coolOnDeltaC = DEFAULT_COOL_ON_DELTA_C;
  float heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
  float holdDeltaC = DEFAULT_HOLD_DELTA_C;
  float tempOffsetC = DEFAULT_TEMP_OFFSET_C;
  bool unitsFahrenheit = true;
  uint8_t brightness = DEFAULT_BRIGHTNESS;
  bool hydrometerBleEnabled = true;
  HydrometerScanType hydrometerScanType = HydrometerScanType::Unknown;
  bool gradualCrashEnabled = false;
  float gradualCrashStepC = DEFAULT_GRADUAL_CRASH_STEP_C;
  uint32_t gradualCrashStepIntervalHours = DEFAULT_GRADUAL_CRASH_STEP_INTERVAL_HOURS;
  UserMode mode = UserMode::Off;
  RuntimeState runtimeState = RuntimeState::Boot;
  FaultCode faultCode = FaultCode::None;
  bool heaterOn = false;
  bool pumpOn = false;
  uint32_t pumpOffElapsedMs = 0;
  bool notReaching = false;
  bool longOutput = false;
  bool demoSensor = false;
};

enum class InfluxExportTarget : uint8_t {
  V1 = 1,
  V2 = 2,
  V3 = 3,
  VictoriaMetrics = 4,
};

enum class ExportPayloadScope : uint8_t {
  ControllerAndHydrometer = 0,
  HydrometerOnly = 1,
};

struct InfluxConfig {
  bool enabled = false;
  InfluxExportTarget target = InfluxExportTarget::V1;
  String url = "";
  String database = "fermentdial";
  String retentionPolicy = "";
  String username = "";
  String password = "";
  String org = "";
  String bucket = "fermentdial";
  String token = "";
  String measurement = "fermentdial";
  uint32_t intervalSeconds = 30;
};

struct MqttConfig {
  bool enabled = false;
  ExportPayloadScope payloadScope = ExportPayloadScope::ControllerAndHydrometer;
  bool haDiscovery = true;
  String discoveryPrefix = "homeassistant";
  String host = "";
  uint16_t port = 1883;
  String username = "";
  String password = "";
  String baseTopic = "fermentdial";
  uint32_t intervalSeconds = 30;
};

struct BrewfatherConfig {
  bool enabled = false;
  ExportPayloadScope payloadScope = ExportPayloadScope::ControllerAndHydrometer;
  String url = "https://log.brewfather.net/stream";
  String loggingId = "";
  String deviceName = "";
  uint32_t intervalSeconds = 900;
};

using FirmwareUpdateSafetyCallback = void (*)();
using FactoryResetCallback = void (*)();
using FermentResetCallback = void (*)();
using BrightnessPreviewCallback = void (*)(uint8_t brightness);
struct ScreenFrame {
  const uint8_t *data = nullptr;
  size_t len = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t stride = 0;
};

using ScreenFrameProvider = ScreenFrame (*)();

enum class ScreenInputKind : uint8_t {
  Tap,
  Swipe,
  Hold,
  Scroll,
};

struct ScreenInputEvent {
  ScreenInputKind kind = ScreenInputKind::Tap;
  int16_t x = 0;
  int16_t y = 0;
  int16_t dx = 0;
  int16_t dy = 0;
  int16_t delta = 0;
};

using ScreenInputCallback = void (*)(const ScreenInputEvent &event);

class NetworkManager {
public:
  void begin(const Settings &settings, const HydrometerManager &hydrometer);
  void setFirmwareUpdateSafetyCallback(FirmwareUpdateSafetyCallback callback);
  void setFactoryResetCallback(FactoryResetCallback callback);
  void setFermentResetCallback(FermentResetCallback callback);
  void clearLiveHistory();
  void setBrightnessPreviewCallback(BrightnessPreviewCallback callback);
  void setScreenFrameProvider(ScreenFrameProvider provider);
  void setScreenInputCallback(ScreenInputCallback callback);
  void serviceWeb();
  void setEventLog(EventLog *log) { _eventLog = log; }
  void update(uint32_t nowMs, Settings &settings);
  void publishState(uint32_t nowMs, const Settings &settings,
                    const TemperatureSensor &sensor,
                    const FermentationController &controller,
                    const HydrometerManager &hydrometer);
  NetworkSnapshot snapshot() const { return _snapshot; }
  bool consumeSettingsChanged();
  bool requestSetupPortal();

private:
  NetworkSnapshot _snapshot;
  FirmwareUpdateSafetyCallback _firmwareUpdateSafetyCallback = nullptr;
  FactoryResetCallback _factoryResetCallback = nullptr;
  FermentResetCallback _fermentResetCallback = nullptr;
  BrightnessPreviewCallback _brightnessPreviewCallback = nullptr;
  ScreenFrameProvider _screenFrameProvider = nullptr;
  ScreenInputCallback _screenInputCallback = nullptr;
  bool _firmwareUpdateInProgress = false;
  bool _firmwareUpdateHadError = false;
  bool _firmwareUpdateOk = false;
  String _firmwareUpdateError = "";
  bool _settingsChanged = false;
  uint32_t _lastPublishMs = 0;
  uint32_t _lastWifiAttemptMs = 0;
  uint32_t _lastInfluxPublishMs = 0;
  uint32_t _lastBrewfatherPublishMs = 0;
  WebStatus _webStatus;
  // Rolling in-RAM dashboard sparkline (lost on reboot). The graph uses the
  // always-on LittleFS CSV; oldest live samples drop when this ring is full.
  // Temperatures are deci-degrees C; gravity is SG * 10000.
  static constexpr uint16_t HISTORY_LEN = 720;
  static constexpr uint32_t HISTORY_INTERVAL_MS = 60000;
  int16_t _historyTempC[HISTORY_LEN] = {0};
  int16_t _historyHydroTempC[HISTORY_LEN] = {0};
  uint16_t _historyGravity[HISTORY_LEN] = {0};
  bool _historyHydroTempValid[HISTORY_LEN] = {false};
  bool _historyGravityValid[HISTORY_LEN] = {false};
  uint16_t _historyCount = 0;
  uint16_t _historyHead = 0;
  uint32_t _lastSampleMs = 0;
  InfluxConfig _influx;
  int _lastInfluxStatusCode = 0;
  String _lastInfluxStatus = "Disabled";
  MqttConfig _mqttConfig;
  uint32_t _lastMqttPublishMs = 0;
  uint32_t _lastMqttAttemptMs = 0;
  String _lastMqttStatus = "Disabled";
  BrewfatherConfig _brewfather;
  int _lastBrewfatherStatusCode = 0;
  String _lastBrewfatherStatus = "Disabled";
  // Hydrometer slugs whose HA discovery configs have already been published.
  // Cleared on (re)connect and on settings changes so configs get re-announced.
  static constexpr uint8_t HA_MAX_ANNOUNCED = 8;
  String _haAnnounced[HA_MAX_ANNOUNCED];
  uint8_t _haAnnouncedCount = 0;
  String _wifiSsid;
  String _wifiPassword;
  String _hostname;
  // Admin password for the config pages; blank disables the lock. The session
  // token is regenerated on each login and lives only in RAM.
  String _adminPassword;
  String _sessionToken;
  Settings *_settings = nullptr;
  const HydrometerManager *_hydrometer = nullptr;
  EventLog *_eventLog = nullptr;

#if FERM_ENABLE_NETWORK
  Preferences _prefs;
  WebServer _server{80};
  DNSServer _dns;
  bool _apMode = false;
  bool _serverStarted = false;
  WiFiClient _mqttClient;
  PubSubClient _mqtt{_mqttClient};
#endif

  void startWifi(uint32_t nowMs);
  void startSetupPortal();
  void startWebServer();
  // Web admin auth: a session cookie gates the config pages/POSTs while the
  // live dashboard, metrics and status stay public. AP setup mode is exempt.
  bool isAuthed();
  bool requireAuth();
  void handleLogin();
  void handleLogout();
  void handleSecurityPost();
  void handleFactoryResetPost();
  String loginHtml(bool showError) const;
  String newSessionToken();
  void handleWifiScan();
  void handleSettingsPost();
  void handleScreenInputPost();
  void handleDeviceSettingsPost();
  void previewWebBrightness(int raw);
  void applyWebBrightness(int raw);
  void handleFirmwareUpload();
  void handleInfluxSettingsPost();
  void loadInfluxConfig();
  void saveInfluxConfig();
  void publishInflux(uint32_t nowMs);
  void handleMqttSettingsPost();
  void loadMqttConfig();
  void saveMqttConfig();
  void mqttConnect(uint32_t nowMs);
  void publishMqtt(uint32_t nowMs);
  void handleBrewfatherSettingsPost();
  void loadBrewfatherConfig();
  void saveBrewfatherConfig();
  void publishBrewfather(uint32_t nowMs);
  String brewfatherPayload(uint32_t nowMs, bool &hasValue) const;
  String brewfatherHydrometerPayload(const HydrometerReading &reading,
                                     const String &deviceName,
                                     bool &hasValue) const;
  // Hydrometer-only MQTT: one Home Assistant device per discovered hydrometer.
  void publishMqttHydrometers(uint32_t nowMs);
  String mqttHydrometerState(const HydrometerReading &reading,
                             uint32_t nowMs) const;
  void publishHaDiscovery(const HydrometerReading &reading, const String &slug,
                          const String &stateTopic);
  bool haAlreadyAnnounced(const String &slug) const;
  void haMarkAnnounced(const String &slug);
  void haResetAnnounced();
  String influxLineProtocol(uint32_t nowMs) const;
  bool parseMode(const String &value, UserMode &mode) const;
  String statusJson(uint32_t nowMs) const;
  String programJson() const;
  String selfCheckJson(uint32_t nowMs) const;
  void streamHistoryFile(const char *path);
  void recordHistory(uint32_t nowMs, bool valid, float tempC,
                     const HydrometerReading &hydro);
  String historyJson() const;
  String settingsConfigJson() const;
  String metricsText(uint32_t nowMs) const;
  String assetVersionQuery() const;
  String webAppHead(const char *title, bool appCss,
                    const char *extraHead = nullptr) const;
  String pageHtml() const;
  String setupHtml() const;
  String settingsHtml() const;
  String firmwareHtml() const;
  String framebufferViewerHtml() const;

};

} // namespace ferm
