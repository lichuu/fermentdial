#pragma once

#include "config.h"
#include "control.h"

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
  uint8_t activeProfile = static_cast<uint8_t>(ProfileSlot::Ferment);
  float liveTargetC = DEFAULT_TARGET_C;
  float coolOnDeltaC = DEFAULT_COOL_ON_DELTA_C;
  float heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
  float holdDeltaC = DEFAULT_HOLD_DELTA_C;
  float tempOffsetC = DEFAULT_TEMP_OFFSET_C;
  bool unitsFahrenheit = true;
  uint8_t brightness = DEFAULT_BRIGHTNESS;
  UserMode mode = UserMode::Off;
  RuntimeState runtimeState = RuntimeState::Boot;
  FaultCode faultCode = FaultCode::None;
  bool heaterOn = false;
  bool pumpOn = false;
  bool demoSensor = false;
};

enum class InfluxExportTarget : uint8_t {
  V1 = 1,
  V2 = 2,
  V3 = 3,
  VictoriaMetrics = 4,
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
  String host = "";
  uint16_t port = 1883;
  String username = "";
  String password = "";
  String baseTopic = "fermentdial";
  uint32_t intervalSeconds = 30;
};

using FirmwareUpdateSafetyCallback = void (*)();

class NetworkManager {
public:
  void begin(const Settings &settings);
  void setFirmwareUpdateSafetyCallback(FirmwareUpdateSafetyCallback callback);
  void update(uint32_t nowMs, Settings &settings);
  void publishState(uint32_t nowMs, const Settings &settings,
                    const TemperatureSensor &sensor,
                    const FermentationController &controller);
  NetworkSnapshot snapshot() const { return _snapshot; }
  bool consumeSettingsChanged();
  bool requestSetupPortal();

private:
  NetworkSnapshot _snapshot;
  FirmwareUpdateSafetyCallback _firmwareUpdateSafetyCallback = nullptr;
  bool _firmwareUpdateInProgress = false;
  bool _firmwareUpdateHadError = false;
  bool _firmwareUpdateOk = false;
  String _firmwareUpdateError = "";
  bool _settingsChanged = false;
  uint32_t _lastPublishMs = 0;
  uint32_t _lastWifiAttemptMs = 0;
  uint32_t _lastInfluxPublishMs = 0;
  WebStatus _webStatus;
  // Rolling temperature history for the dashboard sparkline: deci-degrees C,
  // sampled at a fixed cadence so points are evenly spaced (no timestamps).
  static constexpr uint8_t HISTORY_LEN = 120;
  static constexpr uint32_t HISTORY_INTERVAL_MS = 30000;
  int16_t _history[HISTORY_LEN] = {0};
  uint8_t _historyCount = 0;
  uint8_t _historyHead = 0;
  uint32_t _lastSampleMs = 0;
  InfluxConfig _influx;
  int _lastInfluxStatusCode = 0;
  String _lastInfluxStatus = "Disabled";
  MqttConfig _mqttConfig;
  uint32_t _lastMqttPublishMs = 0;
  uint32_t _lastMqttAttemptMs = 0;
  String _lastMqttStatus = "Disabled";
  String _wifiSsid;
  String _wifiPassword;
  String _hostname;
  // Admin password for the config pages; blank disables the lock. The session
  // token is regenerated on each login and lives only in RAM.
  String _adminPassword;
  String _sessionToken;

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
  String loginHtml(bool showError) const;
  String newSessionToken();
  void handleWifiScan();
  void handleSettingsPost();
  void handleDeviceSettingsPost();
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
  String influxLineProtocol() const;
  bool parseMode(const String &value, UserMode &mode) const;
  String statusJson() const;
  void recordHistory(uint32_t nowMs, bool valid, float tempC);
  String historyJson() const;
  String metricsText() const;
  String pageHtml() const;
  String setupHtml() const;
  String settingsHtml() const;
  String firmwareHtml() const;

#if FERM_ENABLE_NETWORK
  Settings *_settings = nullptr;
#endif
};

} // namespace ferm
