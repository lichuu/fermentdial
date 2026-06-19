#include "time_sync.h"

#include "config.h"

#if FERM_ENABLE_NETWORK
#include <time.h>
#endif

namespace ferm {

namespace {

#if FERM_ENABLE_NETWORK
constexpr time_t MIN_VALID_EPOCH = 1609459200;  // 2021-01-01 UTC

bool epochValid(time_t t) { return t >= MIN_VALID_EPOCH; }

bool _ntpRequested = false;
uint32_t _lastNtpAttemptMs = 0;
constexpr uint32_t NTP_RETRY_MS = 60000;
#endif

}  // namespace

void timeSyncBegin() {
#if FERM_ENABLE_NETWORK
  _ntpRequested = false;
  _lastNtpAttemptMs = 0;
#endif
}

void timeSyncLoop(uint32_t nowMs, bool wifiConnected) {
#if FERM_ENABLE_NETWORK
  if (!wifiConnected) {
    _ntpRequested = false;
    return;
  }
  if (epochValid(time(nullptr))) {
    return;
  }
  if (!_ntpRequested || nowMs - _lastNtpAttemptMs >= NTP_RETRY_MS) {
    _lastNtpAttemptMs = nowMs;
    _ntpRequested = true;
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  }
#else
  (void)nowMs;
  (void)wifiConnected;
#endif
}

Timestamp nowEpochOrUptime(uint32_t nowMs) {
  Timestamp ts;
#if FERM_ENABLE_NETWORK
  const time_t now = time(nullptr);
  if (epochValid(now)) {
    ts.seconds = static_cast<uint32_t>(now);
    ts.wallClock = true;
    return ts;
  }
#endif
  ts.seconds = nowMs / 1000U;
  ts.wallClock = false;
  return ts;
}

bool timeSyncWallClock() {
#if FERM_ENABLE_NETWORK
  return epochValid(time(nullptr));
#else
  return false;
#endif
}

}  // namespace ferm
