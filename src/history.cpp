#include "history.h"

#include <LittleFS.h>

namespace ferm {

void HistoryLog::begin() {
  _lastSampleMs = 0;
  _sampled = false;
  File f = LittleFS.open(HISTORY_CSV_PATH, "r");
  _size = f ? f.size() : 0;
  if (f) {
    f.close();
  }
}

bool HistoryLog::due(uint32_t nowMs) const {
  return !_sampled || (nowMs - _lastSampleMs) >= HISTORY_CSV_INTERVAL_MS;
}

void HistoryLog::markSampled(uint32_t nowMs) {
  _lastSampleMs = nowMs;
  _sampled = true;
}

void HistoryLog::clear() {
  LittleFS.remove(HISTORY_CSV_PATH);
  LittleFS.remove(HISTORY_CSV_PRIOR_PATH);
  _size = 0;
  _sampled = false;
  _lastSampleMs = 0;
}

void HistoryLog::append(const String &row) {
  // Rotate when the current file would exceed the cap: drop the old prior file,
  // move current to prior, and start a fresh current file.
  if (_size + row.length() > HISTORY_CSV_MAX_BYTES) {
    LittleFS.remove(HISTORY_CSV_PRIOR_PATH);
    LittleFS.rename(HISTORY_CSV_PATH, HISTORY_CSV_PRIOR_PATH);
    _size = 0;
  }
  File f = LittleFS.open(HISTORY_CSV_PATH, "a");
  if (!f) {
    return;
  }
  f.print(row);
  _size += row.length();
  f.close();
}

}  // namespace ferm
