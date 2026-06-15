#pragma once

#include "config.h"
#include "control.h"

#if FERM_ENABLE_NETWORK
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#endif

namespace ferm {

struct NetworkSnapshot {
  bool wifiConnected = false;
  bool mqttConnected = false;
  bool otaEnabled = false;
  bool wifiEnabled = false;
  bool wifiConfigured = false;
  String ipAddress = "";
  String ssid = "";
  String status = "WiFi OFF";
};

struct WebStatus {
  bool tempValid = false;
  float tempC = NAN;
  ProfileSettings profiles[PROFILE_COUNT];
  uint8_t activeProfile = static_cast<uint8_t>(ProfileSlot::Ferment);
  float coolOnDeltaC = DEFAULT_COOL_ON_DELTA_C;
  float heatOnDeltaC = DEFAULT_HEAT_ON_DELTA_C;
  float holdDeltaC = DEFAULT_HOLD_DELTA_C;
  float tempOffsetC = DEFAULT_TEMP_OFFSET_C;
  bool unitsFahrenheit = true;
  UserMode mode = UserMode::Off;
  RuntimeState runtimeState = RuntimeState::Boot;
  FaultCode faultCode = FaultCode::None;
  bool heaterOn = false;
  bool pumpOn = false;
  bool demoSensor = false;
};

class NetworkManager {
public:
  void begin(const Settings &settings);
  void update(uint32_t nowMs, Settings &settings);
  void publishState(uint32_t nowMs, const Settings &settings,
                    const TemperatureSensor &sensor,
                    const FermentationController &controller);
  NetworkSnapshot snapshot() const { return _snapshot; }
  bool consumeSettingsChanged();
  bool requestSetupPortal();

private:
  NetworkSnapshot _snapshot;
  bool _settingsChanged = false;
  uint32_t _lastPublishMs = 0;
  uint32_t _lastWifiAttemptMs = 0;
  WebStatus _webStatus;

#if FERM_ENABLE_NETWORK
  Preferences _prefs;
  WebServer _server{80};
  DNSServer _dns;
  bool _apMode = false;
  bool _serverStarted = false;
  String _wifiSsid;
  String _wifiPassword;
#endif

  void startWifi(uint32_t nowMs);
  void startSetupPortal();
  void startWebServer();
  void handleSettingsPost();
  bool parseMode(const String &value, UserMode &mode) const;
  String statusJson() const;
  String pageHtml() const;
  String setupHtml() const;

#if FERM_ENABLE_NETWORK
  Settings *_settings = nullptr;
#endif
};

} // namespace ferm
