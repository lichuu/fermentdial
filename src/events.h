#pragma once

#include <Arduino.h>

#include "time_sync.h"

namespace ferm {

enum class EventType : uint8_t {
  Info = 0,
  ProgramStep = 1,
  ProgramDone = 2,
  HydrometerStale = 3,
  NotReachingTarget = 4,
  SensorFault = 5,
  InterlockFault = 6,
  LongRuntime = 7,
};

struct Event {
  uint32_t ts = 0;        // epoch seconds when wallClock, else uptime seconds
  bool wallClock = false;
  EventType type = EventType::Info;
  String message;
};

// Bounded, reboot-persistent log of notable controller events. Backed by a RAM
// ring buffer that is flushed to LittleFS so history survives a reboot.
class EventLog {
 public:
  static constexpr uint8_t CAPACITY = 40;

  void begin();  // mount-independent; loads any persisted log from LittleFS
  void add(EventType type, const String &message, const Timestamp &ts);
  // Convert same-boot uptime stamps to epoch once wall clock is available.
  void upgradeUptimeTimestamps(uint32_t nowMs);
  String toJson() const;
  void flush();  // persist to LittleFS if dirty
  void clear();  // drop RAM buffer and delete the LittleFS log
  bool dirty() const { return _dirty; }
  uint8_t count() const { return _count; }

 private:
  Event _events[CAPACITY];
  uint8_t _count = 0;
  uint8_t _head = 0;  // index of the oldest entry
  bool _dirty = false;

  void push(const Event &event);
};

}  // namespace ferm
