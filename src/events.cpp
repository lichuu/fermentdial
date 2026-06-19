#include "events.h"

#include <LittleFS.h>

namespace ferm {

namespace {

constexpr const char *EVENTS_PATH = "/events.log";

const char *typeName(EventType type) {
  switch (type) {
  case EventType::ProgramStep:
    return "program-step";
  case EventType::ProgramDone:
    return "program-done";
  case EventType::HydrometerStale:
    return "hydrometer-stale";
  case EventType::NotReachingTarget:
    return "not-reaching-target";
  case EventType::SensorFault:
    return "sensor-fault";
  case EventType::InterlockFault:
    return "interlock-fault";
  case EventType::LongRuntime:
    return "long-runtime";
  case EventType::Info:
  default:
    return "info";
  }
}

String jsonEscape(const String &value) {
  String out = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '"' || c == '\\') {
      out += '\\';
    }
    if (c >= 0x20) {
      out += c;
    }
  }
  out += "\"";
  return out;
}

}  // namespace

void EventLog::push(const Event &event) {
  if (_count < CAPACITY) {
    _events[(_head + _count) % CAPACITY] = event;
    _count++;
  } else {
    _events[_head] = event;
    _head = (_head + 1) % CAPACITY;
  }
}

void EventLog::begin() {
  _count = 0;
  _head = 0;
  _dirty = false;
  bool droppedBoot = false;
  File f = LittleFS.open(EVENTS_PATH, "r");
  if (!f) {
    return;
  }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      continue;
    }
    // Format: ts|wallClock|type|message  (message is the rest of the line).
    int p1 = line.indexOf('|');
    int p2 = line.indexOf('|', p1 + 1);
    int p3 = line.indexOf('|', p2 + 1);
    if (p1 < 0 || p2 < 0 || p3 < 0) {
      continue;
    }
    Event e;
    e.ts = strtoul(line.substring(0, p1).c_str(), nullptr, 10);
    e.wallClock = line.substring(p1 + 1, p2).toInt() != 0;
    e.type = static_cast<EventType>(line.substring(p2 + 1, p3).toInt());
    e.message = line.substring(p3 + 1);
    if (e.type == EventType::Info && e.message.startsWith(F("Booted "))) {
      droppedBoot = true;
      continue;
    }
    push(e);
  }
  f.close();
  if (droppedBoot) {
    _dirty = true;
  }
}

void EventLog::add(EventType type, const String &message, const Timestamp &ts) {
  Event e;
  e.ts = ts.seconds;
  e.wallClock = ts.wallClock;
  e.type = type;
  e.message = message;
  push(e);
  _dirty = true;
}

void EventLog::upgradeUptimeTimestamps(uint32_t nowMs) {
  const Timestamp now = nowEpochOrUptime(nowMs);
  if (!now.wallClock) {
    return;
  }
  const uint32_t uptimeSec = nowMs / 1000U;
  bool changed = false;
  for (uint8_t i = 0; i < _count; ++i) {
    Event &e = _events[(_head + i) % CAPACITY];
    if (!e.wallClock && e.ts <= uptimeSec) {
      e.ts = now.seconds - (uptimeSec - e.ts);
      e.wallClock = true;
      changed = true;
    }
  }
  if (changed) {
    _dirty = true;
  }
}

void EventLog::flush() {
  if (!_dirty) {
    return;
  }
  File f = LittleFS.open(EVENTS_PATH, "w");
  if (!f) {
    return;
  }
  for (uint8_t i = 0; i < _count; ++i) {
    const Event &e = _events[(_head + i) % CAPACITY];
    f.print(e.ts);
    f.print('|');
    f.print(e.wallClock ? 1 : 0);
    f.print('|');
    f.print(static_cast<int>(e.type));
    f.print('|');
    f.println(e.message);
  }
  f.close();
  _dirty = false;
}

String EventLog::toJson() const {
  String json = "[";
  // Newest first.
  for (uint8_t i = 0; i < _count; ++i) {
    const Event &e = _events[(_head + _count - 1 - i) % CAPACITY];
    if (i > 0) {
      json += ",";
    }
    json += "{\"ts\":" + String(e.ts) +
            ",\"wallClock\":" + String(e.wallClock ? "true" : "false") +
            ",\"type\":" + String(static_cast<int>(e.type)) +
            ",\"typeName\":\"" + typeName(e.type) + "\"" +
            ",\"message\":" + jsonEscape(e.message) + "}";
  }
  json += "]";
  return json;
}

}  // namespace ferm
