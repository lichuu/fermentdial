#pragma once

#include <string>

#include "config.h"

#if FERM_ENABLE_HYDROMETER_BLE
#include <NimBLEDevice.h>
#endif

namespace ferm {

enum class HydrometerType : uint8_t {
  Unknown = 0,
  Tilt = 1,
  Rapt = 2,
};

struct HydrometerReading {
  bool valid = false;
  bool selected = false;
  bool discovered = false;
  HydrometerType type = HydrometerType::Unknown;
  String key = "";
  String label = "";
  String name = "";
  String address = "";
  String color = "";
  float gravity = NAN;
  float temperatureC = NAN;
  int8_t rssi = 0;
  float batteryV = NAN;
  float gravityVelocity = NAN;
  bool gravityVelocityValid = false;
  float originalGravity = NAN;
  float abv = NAN;
  uint32_t stableSeconds = 0;
  uint32_t lastSeenMs = 0;
  bool stale = false;
  uint8_t rawVersion = 0;
};

class HydrometerManager {
 public:
  static constexpr uint8_t MAX_DEVICES = 8;

  void begin();
  bool update(uint32_t nowMs, Settings &settings);
  void markStableCheckpoint(uint32_t nowMs);
  bool enabled(const Settings &settings) const;
  uint8_t deviceCount() const { return _deviceCount; }
  const HydrometerReading &device(uint8_t index) const;
  const HydrometerReading *findByKey(const String &key) const;
  HydrometerReading selectedReading(const Settings &settings,
                                    uint32_t nowMs) const;

 private:
  static constexpr uint32_t SCAN_PERIOD_MS = 10000;
  static constexpr uint32_t SCAN_WINDOW_MS = 2000;
  static constexpr uint32_t STALE_AFTER_MS = 5UL * 60UL * 1000UL;
  static constexpr float GRAVITY_STABLE_DELTA = 0.001f;

  HydrometerReading _devices[MAX_DEVICES];
  uint8_t _deviceCount = 0;
  bool _scannerReady = false;
  bool _scanRunning = false;
  uint32_t _nextScanAtMs = 0;
  uint32_t _stableStartMs = 0;
  HydrometerScanType _scanType = HydrometerScanType::Unknown;

#if FERM_ENABLE_HYDROMETER_BLE
  class ScanCallbacks : public NimBLEScanCallbacks {
   public:
    explicit ScanCallbacks(HydrometerManager *owner) : _owner(owner) {}
    void onResult(const NimBLEAdvertisedDevice *device) override;

   private:
    HydrometerManager *_owner;
  };

  ScanCallbacks _scanCallbacks{this};
#endif

  void clearDevices();
  HydrometerReading *findOrCreate(const String &key, HydrometerType type);
  static bool readingIsStale(const HydrometerReading &reading, uint32_t nowMs);

#if FERM_ENABLE_HYDROMETER_BLE
  void startScan(uint32_t nowMs);
  void stopScan();
  void handleAdvertised(const NimBLEAdvertisedDevice &device);
  bool decodeTilt(const NimBLEAdvertisedDevice &device,
                  const std::string &manufacturerData,
                  HydrometerReading &reading) const;
  bool decodeRapt(const NimBLEAdvertisedDevice &device,
                  const std::string &manufacturerData,
                  HydrometerReading &reading) const;
  static String normalizeAddress(const NimBLEAddress &address);
  static String hexUuid(const uint8_t *bytes);
#endif
};

} // namespace ferm
