#pragma once

#include <Preferences.h>

#include "config.h"

namespace ferm {

class SettingsStorage {
 public:
  void begin();
  // Returns false only on a fresh NVS store (no fermName or legacy version key).
  // Otherwise reads persisted settings and returns true.
  bool load(Settings &settings);
  void scheduleSave(uint32_t nowMs);
  void loop(uint32_t nowMs, const Settings &settings);
  void saveNow(const Settings &settings);

 private:
  Preferences _prefs;
  bool _pending = false;
  uint32_t _saveAtMs = 0;
};

}  // namespace ferm

