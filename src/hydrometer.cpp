#include "hydrometer.h"

#include <cmath>
#include <cstring>

namespace ferm {

namespace {

// True only for the synthetic demo device; compiles to false in real builds.
bool isDemoKey(const String &key) {
#if FERM_DEMO_SENSOR
  return key == DEMO_HYDROMETER_KEY;
#else
  (void)key;
  return false;
#endif
}

constexpr uint8_t TILT_UUIDS[][16] = {
    {0xA4, 0x95, 0xBB, 0x10, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // red
    {0xA4, 0x95, 0xBB, 0x20, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // green
    {0xA4, 0x95, 0xBB, 0x30, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // black
    {0xA4, 0x95, 0xBB, 0x40, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // purple
    {0xA4, 0x95, 0xBB, 0x50, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // orange
    {0xA4, 0x95, 0xBB, 0x60, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // blue
    {0xA4, 0x95, 0xBB, 0x70, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // yellow
    {0xA4, 0x95, 0xBB, 0x80, 0xC5, 0xB1, 0x4B, 0x44, 0xB5, 0x12, 0x13, 0x70,
     0xF0, 0x2D, 0x74, 0xDE}, // pink
};

constexpr const char *TILT_COLORS[] = {"red",    "green", "black", "purple",
                                       "orange", "blue",  "yellow", "pink"};

constexpr float TILT_TEMP_MIN_F = MIN_VALID_TEMP_F;
constexpr float TILT_TEMP_MAX_F = MAX_VALID_TEMP_F;
// Classic Tilt gravity raw is ~900-1200; Tilt Pro is ~9000-12000.
constexpr uint16_t TILT_PRO_GRAVITY_THRESHOLD = 5000;
constexpr uint16_t RAPT_MANUFACTURER_ID = 0x4152; // "RA"
constexpr uint16_t APPLE_MANUFACTURER_ID = 0x004C;

uint16_t readU16BE(const uint8_t *p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint16_t readU16LE(const uint8_t *p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

int16_t readS16BE(const uint8_t *p) {
  return static_cast<int16_t>((p[0] << 8) | p[1]);
}

float readF32BE(const uint8_t *p) {
  uint32_t raw = (static_cast<uint32_t>(p[0]) << 24) |
                 (static_cast<uint32_t>(p[1]) << 16) |
                 (static_cast<uint32_t>(p[2]) << 8) | p[3];
  float value;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

bool uuidMatches(const uint8_t *uuid, size_t index) {
  return std::memcmp(uuid, TILT_UUIDS[index], 16) == 0;
}

const char *tiltColorName(const uint8_t *uuid) {
  for (size_t i = 0; i < sizeof(TILT_UUIDS) / sizeof(TILT_UUIDS[0]); ++i) {
    if (uuidMatches(uuid, i)) {
      return TILT_COLORS[i];
    }
  }
  return nullptr;
}

#if FERM_DEMO_SENSOR
float demoLogistic(float p) {
  return 1.0f / (1.0f + expf(-DEMO_FERMENT_LOGISTIC_STEEPNESS *
                              (p - DEMO_FERMENT_LOGISTIC_MIDPOINT)));
}

float demoAttenuation(float p) {
  const float l0 = demoLogistic(0.0f);
  const float l1 = demoLogistic(1.0f);
  const float lp = demoLogistic(p);
  return (lp - l0) / (l1 - l0);
}

float demoActivity(float p) {
  const float l0 = demoLogistic(0.0f);
  const float l1 = demoLogistic(1.0f);
  const float denom = l1 - l0;
  const float lp = demoLogistic(p);
  const float dLp = DEMO_FERMENT_LOGISTIC_STEEPNESS * lp * (1.0f - lp);
  const float daDp = dLp / denom;
  const float maxLp = demoLogistic(DEMO_FERMENT_LOGISTIC_MIDPOINT);
  const float maxDaDp =
      DEMO_FERMENT_LOGISTIC_STEEPNESS * maxLp * (1.0f - maxLp) / denom;
  return maxDaDp > 0.0f ? daDp / maxDaDp : 0.0f;
}

struct DemoFermentSample {
  float gravity = NAN;
  float temperatureC = NAN;
  float originalGravity = NAN;
  float abv = NAN;
  uint32_t stableSeconds = 0;
  int8_t rssi = -55;
  float batteryV = NAN;
};

DemoFermentSample computeDemoFerment(uint32_t nowMs, uint32_t startMs,
                                    float targetC) {
  DemoFermentSample sample;
  const uint32_t elapsedMs = nowMs - startMs;
  const uint32_t cycleMs =
      elapsedMs < DEMO_FERMENT_DURATION_MS ? elapsedMs : DEMO_FERMENT_DURATION_MS;
  const float progress = static_cast<float>(cycleMs) /
                         static_cast<float>(DEMO_FERMENT_DURATION_MS);
  const float attenuation = demoAttenuation(progress);
  const float activity = demoActivity(progress);
  const float rippleG =
      DEMO_FERMENT_GRAVITY_RIPPLE * sinf(static_cast<float>(nowMs) * 0.002f);
  const float rippleT =
      DEMO_FERMENT_TEMP_RIPPLE_C * sinf(static_cast<float>(nowMs) * 0.0015f);

  sample.originalGravity = DEMO_FERMENT_OG;
  sample.gravity = DEMO_FERMENT_OG -
                   (DEMO_FERMENT_OG - DEMO_FERMENT_FG) * attenuation;
  sample.gravity = clampFloat(sample.gravity + rippleG, DEMO_FERMENT_FG,
                              DEMO_FERMENT_OG);
  sample.temperatureC =
      targetC + DEMO_FERMENT_TEMP_BUMP_C * activity + rippleT;
  sample.abv = (sample.originalGravity - sample.gravity) * 131.25f;
  constexpr float kTailFraction = 0.05f;
  if (progress > 1.0f - kTailFraction) {
    sample.stableSeconds =
        (cycleMs - static_cast<uint32_t>((1.0f - kTailFraction) *
                                         static_cast<float>(
                                             DEMO_FERMENT_DURATION_MS))) /
        1000UL;
  }
  sample.batteryV = 3.3f - 0.3f * progress;
  return sample;
}
#endif

} // namespace

#if FERM_ENABLE_HYDROMETER_BLE
void HydrometerManager::ScanCallbacks::onResult(
    const NimBLEAdvertisedDevice *device) {
  if (_owner != nullptr && device != nullptr) {
    _owner->handleAdvertised(*device);
  }
}
#endif

void HydrometerManager::begin() {
#if FERM_DEMO_SENSOR
  _demoFermentStartMs = millis();
#endif
#if FERM_ENABLE_HYDROMETER_BLE
  NimBLEDevice::init("");
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan != nullptr) {
    scan->setActiveScan(true);
    scan->setInterval(1000);
    scan->setWindow(1000);
    scan->setMaxResults(0);
    scan->setDuplicateFilter(0);
    scan->setScanCallbacks(&_scanCallbacks, true);
    _scannerReady = true;
  }
#else
  _scannerReady = false;
#endif
}

bool HydrometerManager::enabled(const Settings &settings) const {
  return FERM_ENABLE_HYDROMETER_BLE && settings.hydrometerBleEnabled;
}

const HydrometerReading &HydrometerManager::device(uint8_t index) const {
  static HydrometerReading empty;
  return index < _deviceCount ? _devices[index] : empty;
}

const HydrometerReading *HydrometerManager::findByKey(const String &key) const {
  if (key.length() == 0) {
    return nullptr;
  }
  for (uint8_t i = 0; i < _deviceCount; ++i) {
    if (_devices[i].key == key) {
      return &_devices[i];
    }
  }
  return nullptr;
}

HydrometerReading HydrometerManager::selectedReading(const Settings &settings,
                                                     uint32_t nowMs) const {
  HydrometerReading reading;
  reading.key = settings.hydrometerSelectionKey;
  reading.selected = reading.key.length() > 0;
  reading.originalGravity = settings.hydrometerOriginalGravity;

  const HydrometerReading *selected = findByKey(settings.hydrometerSelectionKey);
  if (selected == nullptr) {
    if (reading.selected) {
      reading.stale = true;
    }
    return reading;
  }

  reading = *selected;
  reading.selected = true;
  reading.originalGravity = settings.hydrometerOriginalGravity;
  reading.abv = gravityIsValid(settings.hydrometerOriginalGravity) &&
                        gravityIsValid(reading.gravity) &&
                        settings.hydrometerOriginalGravity > reading.gravity
                    ? (settings.hydrometerOriginalGravity - reading.gravity) *
                          131.25f
                    : NAN;
  // Apparent attenuation. OG must sit above 1.000 or the ratio is undefined;
  // early readings can drift slightly above OG, so floor at zero.
  reading.attenuation =
      gravityIsValid(settings.hydrometerOriginalGravity) &&
              gravityIsValid(reading.gravity) &&
              settings.hydrometerOriginalGravity > 1.0f
          ? fmaxf(0.0f, (settings.hydrometerOriginalGravity - reading.gravity) /
                            (settings.hydrometerOriginalGravity - 1.0f) *
                            100.0f)
          : NAN;
  reading.stale = readingIsStale(reading, nowMs);
  if (gravityIsValid(settings.hydrometerStableGravity) &&
      gravityIsValid(reading.gravity) &&
      fabsf(settings.hydrometerStableGravity - reading.gravity) <
          GRAVITY_STABLE_DELTA) {
    const uint32_t endMs = reading.stale ? reading.lastSeenMs : nowMs;
    if (_stableStartMs != 0 && endMs >= _stableStartMs) {
      reading.stableSeconds =
          settings.hydrometerStableSeconds + ((endMs - _stableStartMs) / 1000UL);
    } else {
      reading.stableSeconds = settings.hydrometerStableSeconds;
    }
  } else {
    reading.stableSeconds = settings.hydrometerStableSeconds;
  }
  return reading;
}

bool HydrometerManager::update(uint32_t nowMs, Settings &settings) {
  bool changed = false;
  const HydrometerScanType requestedScanType = settings.hydrometerScanType;
  if (_scanType != requestedScanType) {
    _scanType = requestedScanType;
    clearDevices();
    _stableStartMs = 0;
#if FERM_ENABLE_HYDROMETER_BLE
    stopScan();
#endif
  }

#if FERM_DEMO_SENSOR
  updateDemoDevice(nowMs, settings);
#endif

  if (!enabled(settings)) {
#if FERM_ENABLE_HYDROMETER_BLE
    stopScan();
#endif
    _scanRunning = false;
    _nextScanAtMs = nowMs + SCAN_PERIOD_MS;
    return false;
  }

  if (_scanType == HydrometerScanType::Unknown) {
#if FERM_ENABLE_HYDROMETER_BLE
    stopScan();
#endif
    _nextScanAtMs = nowMs + SCAN_PERIOD_MS;
    return false;
  }

  if (!_scannerReady) {
    begin();
  }

#if FERM_ENABLE_HYDROMETER_BLE
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    return false;
  }

  if (!_scanRunning && nowMs >= _nextScanAtMs) {
    startScan(nowMs);
  }
  if (_scanRunning && !scan->isScanning()) {
    _scanRunning = false;
    _nextScanAtMs = nowMs + (SCAN_PERIOD_MS - SCAN_WINDOW_MS);
  }

  const HydrometerReading *selected =
      findByKey(settings.hydrometerSelectionKey);
  if (selected != nullptr && selected->valid &&
      !readingIsStale(*selected, nowMs) && !isDemoKey(settings.hydrometerSelectionKey)) {
    if (!gravityIsValid(settings.hydrometerOriginalGravity)) {
      settings.hydrometerOriginalGravity = selected->gravity;
      changed = true;
    }
    if (!gravityIsValid(settings.hydrometerStableGravity)) {
      settings.hydrometerStableGravity = selected->gravity;
      _stableStartMs = nowMs;
      changed = true;
    } else if (fabsf(settings.hydrometerStableGravity - selected->gravity) >=
               GRAVITY_STABLE_DELTA) {
      settings.hydrometerStableGravity = selected->gravity;
      settings.hydrometerStableSeconds = 0;
      _stableStartMs = nowMs;
      changed = true;
    } else if (_stableStartMs == 0) {
      _stableStartMs = nowMs;
    }
  }
#endif
  return changed;
}

void HydrometerManager::markStableCheckpoint(uint32_t nowMs) {
  _stableStartMs = nowMs;
}

#if FERM_ENABLE_HYDROMETER_BLE
void HydrometerManager::startScan(uint32_t nowMs) {
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan == nullptr) {
    return;
  }
  _scanRunning = scan->start(SCAN_WINDOW_MS, false, true);
  if (_scanRunning) {
    _nextScanAtMs = nowMs + SCAN_PERIOD_MS;
  }
}
#endif

#if FERM_ENABLE_HYDROMETER_BLE
void HydrometerManager::stopScan() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  if (scan != nullptr && scan->isScanning()) {
    scan->stop();
  }
  _scanRunning = false;
}
#endif

void HydrometerManager::clearDevices() {
  _deviceCount = 0;
  for (uint8_t i = 0; i < MAX_DEVICES; ++i) {
    _devices[i] = HydrometerReading{};
  }
}

HydrometerReading *HydrometerManager::findOrCreate(const String &key,
                                                   HydrometerType type) {
  for (uint8_t i = 0; i < _deviceCount; ++i) {
    if (_devices[i].key == key) {
      return &_devices[i];
    }
  }
  if (_deviceCount < MAX_DEVICES) {
    HydrometerReading &slot = _devices[_deviceCount++];
    slot = HydrometerReading{};
    slot.key = key;
    slot.type = type;
    slot.discovered = true;
    return &slot;
  }
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < _deviceCount; ++i) {
    if (_devices[i].lastSeenMs < _devices[oldest].lastSeenMs) {
      oldest = i;
    }
  }
  HydrometerReading &slot = _devices[oldest];
  slot = HydrometerReading{};
  slot.key = key;
  slot.type = type;
  slot.discovered = true;
  return &slot;
}

bool HydrometerManager::readingIsStale(const HydrometerReading &reading,
                                       uint32_t nowMs) {
  return reading.lastSeenMs == 0 || nowMs - reading.lastSeenMs > STALE_AFTER_MS;
}

#if FERM_DEMO_SENSOR
void HydrometerManager::resetDemoFerment(uint32_t nowMs) {
  _demoFermentStartMs = nowMs;
  _demoCycleComplete = false;
}

bool HydrometerManager::consumeDemoCycleComplete() {
  if (!_demoCycleComplete) {
    return false;
  }
  _demoCycleComplete = false;
  return true;
}

void HydrometerManager::updateDemoDevice(uint32_t nowMs,
                                         const Settings &settings) {
  if (nowMs - _demoFermentStartMs >= DEMO_FERMENT_DURATION_MS) {
    _demoCycleComplete = true;
  }
  HydrometerReading *slot =
      findOrCreate(DEMO_HYDROMETER_KEY, HydrometerType::Tilt);
  if (slot == nullptr) {
    return;
  }

  const DemoFermentSample sample = computeDemoFerment(
      nowMs, _demoFermentStartMs, currentTargetC(settings));
  slot->valid = true;
  slot->stale = false;
  slot->discovered = true;
  slot->type = HydrometerType::Tilt;
  slot->label = DEMO_HYDROMETER_LABEL;
  slot->name = DEMO_HYDROMETER_LABEL;
  slot->color = "green";
  slot->address = "";
  slot->lastSeenMs = nowMs;
  slot->rssi = sample.rssi;
  slot->batteryV = sample.batteryV;
  slot->originalGravity = sample.originalGravity;
  slot->gravity = sample.gravity;
  slot->temperatureC = sample.temperatureC;
  slot->abv = sample.abv;
  slot->stableSeconds = sample.stableSeconds;
  slot->gravityVelocity = NAN;
  slot->gravityVelocityValid = false;
  slot->rawVersion = 0;
}
#endif

#if FERM_ENABLE_HYDROMETER_BLE
String HydrometerManager::normalizeAddress(const NimBLEAddress &address) {
  return String(address.toString().c_str());
}

String HydrometerManager::hexUuid(const uint8_t *bytes) {
  static const char *hex = "0123456789ABCDEF";
  String out;
  out.reserve(32);
  for (uint8_t i = 0; i < 16; ++i) {
    out += hex[(bytes[i] >> 4) & 0x0F];
    out += hex[bytes[i] & 0x0F];
  }
  return out;
}

bool HydrometerManager::decodeTilt(const NimBLEAdvertisedDevice &device,
                                   const std::string &manufacturerData,
                                   HydrometerReading &reading) const {
  const size_t len = manufacturerData.size();
  if (len < 25) {
    return false;
  }
  const uint8_t *data =
      reinterpret_cast<const uint8_t *>(manufacturerData.data());
  if (readU16LE(data) != APPLE_MANUFACTURER_ID || data[2] != 0x02 ||
      data[3] != 0x15) {
    return false;
  }
  const char *color = tiltColorName(data + 4);
  if (color == nullptr) {
    return false;
  }

  // The Tilt Pro reuses the classic UUIDs but broadcasts at 10x resolution:
  // temperature in tenths of a degree F and gravity in ten-thousandths. There
  // is no flag byte, so distinguish by gravity magnitude (classic raw is
  // ~900-1200, Pro is ~9000-12000).
  const uint16_t tempRaw = readU16BE(data + 20);
  const uint16_t gravityRaw = readU16BE(data + 22);
  const bool isPro = gravityRaw > TILT_PRO_GRAVITY_THRESHOLD;
  const float tempF = isPro ? static_cast<float>(tempRaw) / 10.0f
                            : static_cast<float>(tempRaw);
  const float gravity = isPro ? static_cast<float>(gravityRaw) / 10000.0f
                              : static_cast<float>(gravityRaw) / 1000.0f;
  if (tempF < TILT_TEMP_MIN_F || tempF > TILT_TEMP_MAX_F ||
      !gravityIsValid(gravity)) {
    return false;
  }

  reading.valid = true;
  reading.type = HydrometerType::Tilt;
  reading.color = color;
  reading.key = String("tilt:") + color;
  reading.label =
      (isPro ? String("Tilt Pro ") : String("Tilt ")) + String(color);
  reading.name = String(device.getName().c_str());
  reading.address = normalizeAddress(device.getAddress());
  reading.gravity = gravity;
  reading.temperatureC = fToC(tempF);
  reading.rssi = device.getRSSI();
  reading.rawVersion = isPro ? 2 : 1;
  reading.stale = false;
  reading.batteryV = NAN;
  return true;
}

bool HydrometerManager::decodeRapt(const NimBLEAdvertisedDevice &device,
                                   const std::string &manufacturerData,
                                   HydrometerReading &reading) const {
  const size_t len = manufacturerData.size();
  const uint8_t *data =
      reinterpret_cast<const uint8_t *>(manufacturerData.data());
  // The RAPT Pill abuses the manufacturer-data field: the 0x4152 company id
  // reads as ASCII "RA", followed by "PT" and the metrics payload, so the
  // manufacturer data begins with the string "RAPT".
  if (len < 24 || data[0] != 'R' || data[1] != 'A' || data[2] != 'P' ||
      data[3] != 'T') {
    return false;
  }

  reading = HydrometerReading{};
  reading.valid = true;
  reading.type = HydrometerType::Rapt;
  reading.key = String("rapt:") + normalizeAddress(device.getAddress());
  reading.label = "RAPT Pill";
  reading.name = String(device.getName().c_str());
  reading.address = normalizeAddress(device.getAddress());
  reading.rssi = device.getRSSI();

  const uint8_t cc = data[6];
  reading.gravityVelocityValid = cc == 0x01;
  reading.gravityVelocity =
      reading.gravityVelocityValid ? readF32BE(data + 7) : NAN;
  const uint16_t tempRaw = readU16BE(data + 11);
  reading.temperatureC = static_cast<float>(tempRaw) / 128.0f - 273.15f;
  reading.gravity = readF32BE(data + 13) / 1000.0f;
  reading.batteryV = static_cast<float>(readU16LE(data + 22)) / 256.0f;
  reading.rawVersion = cc;

  if (!gravityIsValid(reading.gravity) ||
      reading.temperatureC < MIN_VALID_TEMP_C ||
      reading.temperatureC > MAX_VALID_TEMP_C) {
    return false;
  }
  return true;
}

void HydrometerManager::handleAdvertised(
    const NimBLEAdvertisedDevice &device) {
  if (device.getManufacturerDataCount() == 0) {
    return;
  }

  HydrometerReading decoded;
  bool ok = false;
  for (uint8_t i = 0; i < device.getManufacturerDataCount(); ++i) {
    const std::string manufacturerData = device.getManufacturerData(i);
    if (_scanType == HydrometerScanType::Tilt) {
      ok = decodeTilt(device, manufacturerData, decoded);
    } else if (_scanType == HydrometerScanType::Rapt) {
      ok = decodeRapt(device, manufacturerData, decoded);
    } else if (_scanType == HydrometerScanType::All) {
      ok = decodeTilt(device, manufacturerData, decoded) ||
           decodeRapt(device, manufacturerData, decoded);
    }
    if (ok) {
      break;
    }
  }
  if (!ok) {
    return;
  }

  HydrometerReading *slot = findOrCreate(decoded.key, decoded.type);
  if (slot == nullptr) {
    return;
  }
  const uint32_t nowMs = millis();
  decoded.discovered = true;
  decoded.lastSeenMs = nowMs;
  *slot = decoded;
}
#endif

} // namespace ferm
