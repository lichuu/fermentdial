#pragma once

#include <stdint.h>

namespace ferm {

struct Timestamp {
  uint32_t seconds = 0;
  bool wallClock = false;
};

void timeSyncBegin();
void timeSyncLoop(uint32_t nowMs, bool wifiConnected);
Timestamp nowEpochOrUptime(uint32_t nowMs);
bool timeSyncWallClock();

}  // namespace ferm
