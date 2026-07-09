#include "network.h"

#include "history.h"
#include "network_detail.h"
#include "policy.h"
#include "time_sync.h"

#if FERM_ENABLE_NETWORK
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_random.h>
#endif

#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
#include <Update.h>
#endif

#if FERM_ENABLE_NETWORK
#include "web_assets.h"
#endif

namespace ferm {

using namespace network_detail;

void NetworkManager::begin(const Settings &settings,
                           const HydrometerManager &hydrometer) {
  _snapshot = NetworkSnapshot{};
  _hydrometer = &hydrometer;
  _snapshot.wifiEnabled = FERM_ENABLE_NETWORK;
  _snapshot.otaEnabled = FERM_ENABLE_OTA;
  _hostname = networkHostname(settings.fermenterName);
  _snapshot.hostname = _hostname;
  _webStatus.fermenterName = settings.fermenterName;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.liveTargetC = currentTargetC(settings);
  _webStatus.diacetylRestActive = settings.diacetylRestActive;
  _webStatus.diacetylRestTargetC = settings.diacetylRestTargetC;
  _webStatus.diacetylRestDurationSeconds =
      settings.diacetylRestDurationSeconds;
  _webStatus.diacetylRestRemainingSeconds =
      settings.diacetylRestRemainingSeconds;
  _webStatus.diacetylRestReturnProfile =
      diacetylRestReturnProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.brightness = settings.brightness;
  _webStatus.hydrometerBleEnabled = settings.hydrometerBleEnabled;
  _webStatus.hydrometerScanType = settings.hydrometerScanType;
  _webStatus.gradualCrashEnabled = settings.gradualCrashEnabled;
  _webStatus.gradualCrashStepC = settings.gradualCrashStepC;
  _webStatus.gradualCrashStepIntervalHours = settings.gradualCrashStepIntervalHours;
  _webStatus.mode = settings.mode;

#if FERM_ENABLE_NETWORK
  _prefs.begin("net", false);
  loadInfluxConfig();
  loadMqttConfig();
  loadBrewfatherConfig();
  _wifiSsid = _prefs.getString("ssid", FERM_WIFI_SSID);
  _wifiPassword = _prefs.getString("pass", FERM_WIFI_PASSWORD);
  // Ships unlocked: config pages are open until the user sets a password.
  _adminPassword = _prefs.getString("adminPass", "");
  _snapshot.wifiConfigured = _wifiSsid.length() > 0;
  _snapshot.ssid = _wifiSsid;

  WiFi.persistent(false);

  if (_snapshot.wifiConfigured) {
    WiFi.mode(WIFI_STA);
    startWifi(millis());
    startWebServer();
  } else {
    startSetupPortal();
  }

  // MQTT publishing is driven from update()/publishMqtt(). Home Assistant
  // discovery is not implemented yet; local control continues regardless of
  // Wi-Fi/MQTT availability.
  _mqtt.setBufferSize(4096);
  timeSyncBegin();
#endif

}

void NetworkManager::setFirmwareUpdateSafetyCallback(
    FirmwareUpdateSafetyCallback callback) {
  _firmwareUpdateSafetyCallback = callback;
}

void NetworkManager::setFactoryResetCallback(FactoryResetCallback callback) {
  _factoryResetCallback = callback;
}

void NetworkManager::setFermentResetCallback(FermentResetCallback callback) {
  _fermentResetCallback = callback;
}

void NetworkManager::setBrightnessPreviewCallback(
    BrightnessPreviewCallback callback) {
  _brightnessPreviewCallback = callback;
}

void NetworkManager::setScreenFrameProvider(ScreenFrameProvider provider) {
  _screenFrameProvider = provider;
}

void NetworkManager::setScreenInputCallback(ScreenInputCallback callback) {
  _screenInputCallback = callback;
}

void NetworkManager::serviceWeb() {
#if FERM_ENABLE_NETWORK
  if (_serverStarted) {
    _server.handleClient();
  }
#endif
}

void NetworkManager::previewWebBrightness(int raw) {
  const uint8_t brightness = clampBrightness(raw);
  _webStatus.brightness = brightness;
  if (_brightnessPreviewCallback != nullptr) {
    _brightnessPreviewCallback(brightness);
  }
}

void NetworkManager::applyWebBrightness(int raw) {
  if (_settings == nullptr) {
    return;
  }
  _settings->brightness = clampBrightness(raw);
  _webStatus.brightness = _settings->brightness;
  if (_brightnessPreviewCallback != nullptr) {
    _brightnessPreviewCallback(_settings->brightness);
  }
  _settingsChanged = true;
}

void NetworkManager::update(uint32_t nowMs, Settings &settings) {
#if FERM_ENABLE_NETWORK
  _settings = &settings;
  _snapshot.mqttEnabled = _mqttConfig.enabled;
  const String nextHostname = networkHostname(settings.fermenterName);
  if (nextHostname != _hostname) {
    _hostname = nextHostname;
    _snapshot.hostname = _hostname;
  }
  if (_apMode) {
    _dns.processNextRequest();
    _snapshot.wifiConnected = false;
    _snapshot.ipAddress = WiFi.softAPIP().toString();
    _snapshot.status = "Setup AP";
    return;
  }

  wl_status_t status = WiFi.status();
  _snapshot.wifiConnected = status == WL_CONNECTED;
  timeSyncLoop(nowMs, _snapshot.wifiConnected);
  if (_snapshot.wifiConnected) {
    _snapshot.ipAddress = WiFi.localIP().toString();
    _snapshot.status = _snapshot.ipAddress;
    // Soft-fail mDNS so multi-vessel discovery works when the LAN allows it.
    static bool mdnsStarted = false;
    if (!mdnsStarted) {
      mdnsStarted = MDNS.begin(_hostname.c_str());
      if (mdnsStarted) {
        MDNS.addService("http", "tcp", 80);
      }
    }
  } else {
    _snapshot.ipAddress = "";
    _snapshot.status = "Connecting";
    if (nowMs - _lastWifiAttemptMs >= 15000UL) {
      startWifi(nowMs);
    }
  }

  if (_mqttConfig.enabled && _snapshot.wifiConnected &&
      _mqttConfig.host.length() > 0) {
    if (!_mqtt.connected()) {
      mqttConnect(nowMs);
    }
    _mqtt.loop();
  } else if (_mqtt.connected()) {
    _mqtt.disconnect();
  }
  _snapshot.mqttConnected = _mqtt.connected();
  // TODO: subscribe to setpoint/mode commands. MQTT must never override
  // safety rules; it should only mutate Settings.
#else
  (void)settings;
  (void)nowMs;
#endif
}

void NetworkManager::publishState(uint32_t nowMs, const Settings &settings,
                                  const TemperatureSensor &sensor,
                                  const FermentationController &controller,
                                  const HydrometerManager &hydrometer) {
  _hydrometer = &hydrometer;
  _webStatus.tempValid = sensor.isValid();
  _webStatus.tempC = sensor.temperatureC();
  _webStatus.rawTempC = sensor.rawTemperatureC();
  recordHistory(nowMs, _webStatus.tempValid, _webStatus.tempC,
                hydrometer.selectedReading(settings, nowMs));
  _webStatus.fermenterName = settings.fermenterName;
  for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
    _webStatus.profiles[i] = settings.profiles[i];
  }
  _webStatus.activeProfile = activeProfileIndex(settings);
  _webStatus.liveTargetC = currentTargetC(settings);
  _webStatus.diacetylRestActive = settings.diacetylRestActive;
  _webStatus.diacetylRestTargetC = settings.diacetylRestTargetC;
  _webStatus.diacetylRestDurationSeconds =
      settings.diacetylRestDurationSeconds;
  _webStatus.diacetylRestRemainingSeconds =
      settings.diacetylRestRemainingSeconds;
  _webStatus.diacetylRestReturnProfile =
      diacetylRestReturnProfileIndex(settings);
  _webStatus.coolOnDeltaC = settings.coolOnDeltaC;
  _webStatus.heatOnDeltaC = settings.heatOnDeltaC;
  _webStatus.holdDeltaC = settings.holdDeltaC;
  _webStatus.tempOffsetC = settings.tempOffsetC;
  _webStatus.unitsFahrenheit = settings.unitsFahrenheit;
  _webStatus.brightness = settings.brightness;
  _webStatus.hydrometerBleEnabled = settings.hydrometerBleEnabled;
  _webStatus.hydrometerScanType = settings.hydrometerScanType;
  _webStatus.gradualCrashEnabled = settings.gradualCrashEnabled;
  _webStatus.gradualCrashStepC = settings.gradualCrashStepC;
  _webStatus.gradualCrashStepIntervalHours = settings.gradualCrashStepIntervalHours;
  _webStatus.mode = settings.mode;
  _webStatus.runtimeState = controller.runtimeState();
  _webStatus.faultCode = controller.faultCode();
  _webStatus.heaterOn = controller.heaterOn();
  _webStatus.pumpOn = controller.pumpOn();
  _webStatus.pumpOffElapsedMs = controller.pumpOffElapsedMs(nowMs);
  _webStatus.notReaching = alertNotReachingActive();
  _webStatus.longOutput = alertLongRuntimeActive();
  _webStatus.demoSensor = sensor.demoMode();

  if (nowMs - _lastPublishMs < 5000) {
    return;
  }
  _lastPublishMs = nowMs;

#if FERM_ENABLE_NETWORK
  publishInflux(nowMs);
  publishMqtt(nowMs);
  publishBrewfather(nowMs);
#endif
}

bool NetworkManager::consumeSettingsChanged() {
  bool changed = _settingsChanged;
  _settingsChanged = false;
  return changed;
}

bool NetworkManager::requestSetupPortal() {
#if FERM_ENABLE_NETWORK
  startSetupPortal();
  return true;
#else
  return false;
#endif
}

void NetworkManager::startWifi(uint32_t nowMs) {
#if FERM_ENABLE_NETWORK
  if (_wifiSsid.length() == 0) {
    startSetupPortal();
    return;
  }
  _lastWifiAttemptMs = nowMs;
  _snapshot.status = "Connecting";
  _snapshot.hostname = _hostname;
  WiFi.disconnect(false, false);
  WiFi.setHostname(_hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(_wifiSsid.c_str(), _wifiPassword.c_str());
#else
  (void)nowMs;
#endif
}

void NetworkManager::startSetupPortal() {
#if FERM_ENABLE_NETWORK
  const String ssid = setupApSsid();
  _apMode = true;
  _snapshot.wifiConfigured = false;
  _snapshot.ssid = ssid;
  _snapshot.status = "Setup AP";

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(SETUP_IP, SETUP_GATEWAY, SETUP_MASK);
  WiFi.softAP(ssid.c_str());  // open network for onboarding
  _snapshot.ipAddress = WiFi.softAPIP().toString();
  _dns.start(53, "*", SETUP_IP);
  startWebServer();
#endif
}

void NetworkManager::startWebServer() {
#if FERM_ENABLE_NETWORK
  if (_serverStarted) {
    return;
  }

  // Expose the Cookie header so the session check can read it.
  static const char *kHeaderKeys[] = {"Cookie"};
  _server.collectHeaders(kHeaderKeys, 1);

  _server.on("/", HTTP_GET, [this]() {
    _server.send(200, "text/html; charset=utf-8",
                 _apMode ? setupHtml() : pageHtml());
  });
  _server.on("/dashboard", HTTP_GET, [this]() {
    _server.send(200, "text/html; charset=utf-8", pageHtml());
  });
  _server.on("/favicon.svg", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "max-age=86400");
    _server.send(200, "image/svg+xml", FAVICON_SVG);
  });
  _server.on("/app.js", HTTP_GET, [this]() {
    _server.sendHeader("Content-Encoding", "gzip");
    _server.sendHeader("Cache-Control", "no-cache, must-revalidate");
    _server.send_P(200, "application/javascript; charset=utf-8",
                   reinterpret_cast<PGM_P>(APP_JS_GZ), APP_JS_GZ_LEN);
  });
  _server.on("/app.css", HTTP_GET, [this]() {
    _server.sendHeader("Content-Encoding", "gzip");
    _server.sendHeader("Cache-Control", "no-cache, must-revalidate");
    _server.send_P(200, "text/css; charset=utf-8",
                   reinterpret_cast<PGM_P>(APP_CSS_GZ), APP_CSS_GZ_LEN);
  });
  _server.on("/manifest.webmanifest", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "no-cache, must-revalidate");
    _server.send_P(200, "application/manifest+json; charset=utf-8",
                   reinterpret_cast<PGM_P>(APP_MANIFEST),
                   strlen_P(APP_MANIFEST));
  });
  _server.on("/icon-192.png", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "max-age=86400");
    _server.send_P(200, "image/png",
                   reinterpret_cast<PGM_P>(APP_ICON_PNG), APP_ICON_PNG_LEN);
  });
  _server.on("/apple-touch-icon.png", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "max-age=86400");
    _server.send_P(200, "image/png",
                   reinterpret_cast<PGM_P>(APP_ICON_PNG), APP_ICON_PNG_LEN);
  });
  _server.on("/login", HTTP_GET, [this]() { handleLogin(); });
  _server.on("/login", HTTP_POST, [this]() { handleLogin(); });
  _server.on("/logout", HTTP_GET, [this]() { handleLogout(); });
  _server.on("/logout", HTTP_POST, [this]() { handleLogout(); });
  _server.on("/settings", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html; charset=utf-8", settingsHtml());
  });
  _server.on("/settings/device", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleDeviceSettingsPost();
  });
  _server.on("/settings/security", HTTP_POST,
             [this]() { handleSecurityPost(); });
  _server.on("/settings/factory-reset", HTTP_POST,
             [this]() { handleFactoryResetPost(); });
  _server.on("/settings/influx", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleInfluxSettingsPost();
  });
  _server.on("/settings/mqtt", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleMqttSettingsPost();
  });
  _server.on("/settings/brewfather", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    handleBrewfatherSettingsPost();
  });
  _server.on("/metrics", HTTP_GET, [this]() {
    _server.send(200, "text/plain; version=0.0.4; charset=utf-8",
                 metricsText(millis()));
  });
  _server.on("/wifi", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html; charset=utf-8", setupHtml());
  });
  _server.on("/wifi", HTTP_POST, [this]() {
    if (!requireAuth()) {
      return;
    }
    String ssid = _server.arg("ssid");
    String pass = _server.arg("pass");
    ssid.trim();
    if (ssid.length() == 0) {
      _server.send(400, "text/plain", "SSID is required");
      return;
    }
    _prefs.putString("ssid", ssid);
    if (pass.length() > 0 || _wifiPassword.length() == 0) {
      _prefs.putString("pass", pass);
    }
    _wifiSsid = ssid;
    if (pass.length() > 0 || _wifiPassword.length() == 0) {
      _wifiPassword = pass;
    }
    _server.send(200, "text/html; charset=utf-8",
                 "<!doctype html><meta name='viewport' "
                 "content='width=device-width,initial-scale=1'>"
                 "<body "
                 "style='font-family:sans-serif;padding:2rem;background:#"
                 "071015;color:white'>"
                 "<h1>Saved</h1><p>Rebooting and joining Wi-Fi...</p></body>");
    delay(500);
    ESP.restart();
  });
#if FERM_ENABLE_OTA
  _server.on("/firmware", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "text/html; charset=utf-8", firmwareHtml());
  });
  _server.on(
      "/firmware", HTTP_POST,
      [this]() {
        if (!requireAuth()) {
          return;
        }
        const bool ok = _firmwareUpdateOk && !_firmwareUpdateHadError;
        const String message =
            ok ? "Firmware updated. Rebooting..."
               : (_firmwareUpdateError.length() > 0 ? _firmwareUpdateError
                                                     : "Firmware update failed");
        String html =
            "<!doctype html><meta name='viewport' "
            "content='width=device-width,initial-scale=1'>"
            "<body style='font-family:sans-serif;padding:2rem;background:#"
            "071015;color:white'><h1>" +
            String(ok ? "Update complete" : "Update failed") + "</h1><p>" +
            message + "</p><p><a href='/firmware'>Back</a></p></body>";
        _server.sendHeader("Connection", "close");
        _server.send(ok ? 200 : 500, "text/html; charset=utf-8", html);
        if (ok) {
          delay(800);
          ESP.restart();
        }
      },
      [this]() { handleFirmwareUpload(); });
#endif
  _server.on("/api/status", HTTP_GET,
             [this]() {
               _server.send(200, "application/json", statusJson(millis()));
             });
  _server.on("/api/history", HTTP_GET, [this]() {
    _server.send(200, "application/json", historyJson());
  });
  _server.on("/api/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  _server.on("/api/settings", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "application/json", settingsConfigJson());
  });
  _server.on("/api/settings", HTTP_POST, [this]() { handleSettingsPost(); });
  _server.on("/api/program", HTTP_GET, [this]() {
    if (!requireAuth()) {
      return;
    }
    _server.send(200, "application/json", programJson());
  });
  _server.on("/api/events", HTTP_GET, [this]() {
    _server.send(200, "application/json",
                 _eventLog != nullptr ? _eventLog->toJson() : String("[]"));
  });
  _server.on("/api/selfcheck", HTTP_GET, [this]() {
    _server.send(200, "application/json", selfCheckJson(millis()));
  });
  _server.on("/api/export.json", HTTP_GET, [this]() {
    _server.sendHeader("Cache-Control", "no-store");
    _server.sendHeader("Content-Disposition",
                       "attachment; filename=\"fermentdial-export.json\"");
    _server.send(200, "application/json; charset=utf-8", exportJson(millis()));
  });
  _server.on("/api/history.csv", HTTP_GET, [this]() {
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.sendHeader("Content-Disposition",
                       "attachment; filename=fermentdial-history.csv");
    _server.send(200, "text/csv", "");
    _server.sendContent(HISTORY_CSV_HEADER);
    streamHistoryFile(HISTORY_CSV_PRIOR_PATH);
    streamHistoryFile(HISTORY_CSV_PATH);
    _server.sendContent("");
  });
#if FERM_ENABLE_SCREEN_MIRROR
  _server.on("/api/screen", HTTP_GET, [this]() {
    if (_screenFrameProvider == nullptr) {
      _server.send(503, "text/plain", "Screen mirror unavailable");
      return;
    }
    const ScreenFrame frame = _screenFrameProvider();
    if (frame.data == nullptr || frame.len == 0 || frame.width == 0 ||
        frame.height == 0 || frame.stride == 0) {
      _server.send(503, "text/plain", "No frame");
      return;
    }
    _server.sendHeader("Cache-Control", "no-cache");
    _server.sendHeader("X-Frame-Width", String(frame.width));
    _server.sendHeader("X-Frame-Height", String(frame.height));
    _server.sendHeader("X-Frame-Stride", String(frame.stride));
    _server.send_P(200, "application/octet-stream",
                   reinterpret_cast<PGM_P>(frame.data), frame.len);
  });
  _server.on("/screen", HTTP_GET, [this]() {
    _server.send(200, "text/html; charset=utf-8", framebufferViewerHtml());
  });
  _server.on("/api/screen/input", HTTP_POST, [this]() { handleScreenInputPost(); });
#endif
  _server.onNotFound([this]() {
    _server.sendHeader("Location", "/", true);
    _server.send(302, "text/plain", "");
  });

  _server.begin();
  _serverStarted = true;
#endif
}

void NetworkManager::handleFirmwareUpload() {
#if FERM_ENABLE_NETWORK && FERM_ENABLE_OTA
  HTTPUpload &upload = _server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    _firmwareUpdateInProgress = true;
    _firmwareUpdateHadError = false;
    _firmwareUpdateOk = false;
    _firmwareUpdateError = "";

    // Reject before a single byte is written if the session isn't authorized;
    // the WRITE/END steps bail out on this error so nothing is ever flashed.
    if (!isAuthed()) {
      _firmwareUpdateHadError = true;
      _firmwareUpdateError = "Authentication required";
      return;
    }

    if (_firmwareUpdateSafetyCallback != nullptr) {
      _firmwareUpdateSafetyCallback();
    }

    Serial.print(F("Firmware upload: "));
    Serial.println(upload.filename);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      _firmwareUpdateHadError = true;
      _firmwareUpdateError = "Unable to begin firmware update";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!_firmwareUpdateHadError &&
        Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      _firmwareUpdateHadError = true;
      _firmwareUpdateError = "Firmware write failed";
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (!_firmwareUpdateHadError && Update.end(true)) {
      _firmwareUpdateOk = true;
      Serial.println(F("Firmware upload complete"));
    } else {
      _firmwareUpdateHadError = true;
      if (_firmwareUpdateError.length() == 0) {
        _firmwareUpdateError = "Firmware verification failed";
      }
      Update.printError(Serial);
    }
    _firmwareUpdateInProgress = false;
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    _firmwareUpdateHadError = true;
    _firmwareUpdateError = "Firmware upload aborted";
    _firmwareUpdateInProgress = false;
    Serial.println(F("Firmware upload aborted"));
  }
#endif
}

void NetworkManager::handleWifiScan() {
#if FERM_ENABLE_NETWORK
  const int count = WiFi.scanNetworks(false, true);
  if (count < 0) {
    _server.send(500, "application/json",
                 "{\"error\":\"Wi-Fi scan failed\",\"networks\":[]}");
    return;
  }

  String json = "{\"networks\":[";
  for (int i = 0; i < count; ++i) {
    if (i > 0) {
      json += ",";
    }
    json += "{\"ssid\":" + jsonString(WiFi.SSID(i)) + ",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"channel\":" + String(WiFi.channel(i)) + ",";
    json += "\"secure\":" +
            String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false"
                                                             : "true") +
            "}";
  }
  json += "]}";
  WiFi.scanDelete();
  _server.send(200, "application/json", json);
#endif
}

String NetworkManager::newSessionToken() {
#if FERM_ENABLE_NETWORK
  char buf[33];
  for (int i = 0; i < 4; ++i) {
    snprintf(buf + i * 8, 9, "%08x", static_cast<unsigned>(esp_random()));
  }
  return String(buf);
#else
  return "";
#endif
}

bool NetworkManager::isAuthed() {
#if FERM_ENABLE_NETWORK
  if (_apMode) {
    return true;  // setup portal is already gated by the AP Wi-Fi password
  }
  if (_adminPassword.length() == 0) {
    return true;  // lock explicitly disabled
  }
  if (_sessionToken.length() == 0) {
    return false;  // no active session
  }
  return cookieValue(_server.header("Cookie"), "fdsession") == _sessionToken;
#else
  return true;
#endif
}

bool NetworkManager::requireAuth() {
#if FERM_ENABLE_NETWORK
  if (isAuthed()) {
    return true;
  }
  _server.sendHeader("Location", "/login", true);
  _server.send(302, "text/plain", "");
  return false;
#else
  return true;
#endif
}

void NetworkManager::handleLogin() {
#if FERM_ENABLE_NETWORK
  if (_server.method() == HTTP_GET) {
    if (isAuthed()) {
      _server.sendHeader("Location", "/settings", true);
      _server.send(302, "text/plain", "");
      return;
    }
    _server.send(200, "text/html; charset=utf-8", loginHtml(false));
    return;
  }

  // POST: validate the submitted password and start a session.
  const String pass = _server.arg("password");
  if (_adminPassword.length() > 0 && pass == _adminPassword) {
    _sessionToken = newSessionToken();
    _server.sendHeader("Set-Cookie", "fdsession=" + _sessionToken +
                                         "; Path=/; HttpOnly; SameSite=Lax; "
                                         "Max-Age=604800");
    _server.sendHeader("Location", "/settings", true);
    _server.send(302, "text/plain", "");
    return;
  }
  _server.send(401, "text/html; charset=utf-8", loginHtml(true));
#endif
}

void NetworkManager::handleLogout() {
#if FERM_ENABLE_NETWORK
  _sessionToken = "";
  _server.sendHeader("Set-Cookie", "fdsession=; Path=/; Max-Age=0");
  _server.sendHeader("Location", "/dashboard", true);
  _server.send(302, "text/plain", "");
#endif
}

void NetworkManager::handleFactoryResetPost() {
#if FERM_ENABLE_NETWORK
  if (!requireAuth()) {
    return;
  }

  String confirm = _server.arg("confirm");
  confirm.trim();
  confirm.toUpperCase();
  if (confirm != "RESET") {
    _server.send(400, "application/json",
                 "{\"ok\":false,\"error\":\"type RESET to confirm\"}");
    return;
  }

  Serial.println(F("Factory reset requested; clearing NVS and rebooting"));
  if (_factoryResetCallback != nullptr) {
    _factoryResetCallback();
  }
  _sessionToken = "";
  _prefs.clear();

  _server.send(200, "text/html; charset=utf-8",
               "<!doctype html><meta name='viewport' "
               "content='width=device-width,initial-scale=1'>"
               "<body style='font-family:sans-serif;padding:2rem;"
               "background:#071015;color:#f8fbff'>"
               "<h1>Factory reset</h1>"
               "<p>Settings, Wi-Fi, integrations, and logs were cleared. "
               "Rebooting to setup mode...</p></body>");
  delay(500);
  ESP.restart();
#endif
}

void NetworkManager::handleSecurityPost() {
#if FERM_ENABLE_NETWORK
  if (!requireAuth()) {
    return;
  }
  const String np = _server.arg("newPassword");
  const String cp = _server.arg("confirmPassword");
  if (np != cp) {
    _server.send(400, "text/html; charset=utf-8",
                 "<!doctype html><meta name='viewport' "
                 "content='width=device-width,initial-scale=1'>"
                 "<body style='font-family:sans-serif;padding:2rem;"
                 "background:#071015;color:#f8fbff'><h1>Passwords did not "
                 "match</h1><p><a href='/settings'>Back to settings</a></p>"
                 "</body>");
    return;
  }
  _adminPassword = np;
  _prefs.putString("adminPass", _adminPassword);
  // Re-issue the current session so this browser stays in; any other browser's
  // old token is now invalid. Disabling the lock just clears the session.
  if (_adminPassword.length() > 0) {
    _sessionToken = newSessionToken();
    _server.sendHeader("Set-Cookie", "fdsession=" + _sessionToken +
                                         "; Path=/; HttpOnly; SameSite=Lax; "
                                         "Max-Age=604800");
  } else {
    _sessionToken = "";
    _server.sendHeader("Set-Cookie", "fdsession=; Path=/; Max-Age=0");
  }
  _server.sendHeader("Location", "/settings", true);
  _server.send(302, "text/plain", "");
#endif
}

void NetworkManager::handleDeviceSettingsPost() {
#if FERM_ENABLE_NETWORK
  if (_settings == nullptr) {
    _server.send(503, "text/plain", "settings unavailable");
    return;
  }
  if (_server.hasArg("fermenterName")) {
    _settings->fermenterName = _server.arg("fermenterName");
    sanitizeSettings(*_settings);
    _webStatus.fermenterName = _settings->fermenterName;
    _hostname = networkHostname(_settings->fermenterName);
    _snapshot.hostname = _hostname;
    _settingsChanged = true;
  }
  if (_server.hasArg("brightness")) {
    applyWebBrightness(_server.arg("brightness").toInt());
  }
  _server.sendHeader("Location", "/settings", true);
  _server.send(303, "text/plain", "");
#endif
}


} // namespace ferm
