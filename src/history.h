#pragma once

#include <Arduino.h>

namespace ferm {

// LittleFS-backed rolling CSV time-series. main.cpp appends one row per sample
// interval; network.cpp streams the file(s) for /api/history.csv. Two files are
// retained (current + one rotated prior) so several days of history survive.
constexpr const char *HISTORY_CSV_PATH = "/history.csv";
constexpr const char *HISTORY_CSV_PRIOR_PATH = "/history.1.csv";
constexpr const char *HISTORY_CSV_HEADER =
    "ts,wall,temp_c,target_c,gravity,abv,heater,pump,state,step,hydro_temp_c\n";
constexpr uint32_t HISTORY_CSV_INTERVAL_MS = 120000;  // sample every 2 min
constexpr size_t HISTORY_CSV_MAX_BYTES = 307200;      // rotate at ~300 KB

class HistoryLog {
 public:
  void begin();
  bool due(uint32_t nowMs) const;
  void markSampled(uint32_t nowMs);
  void append(const String &row);  // appends, rotating if the file is full

 private:
  uint32_t _lastSampleMs = 0;
  bool _sampled = false;
  size_t _size = 0;
};

}  // namespace ferm
