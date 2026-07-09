#pragma once

#include "config.h"
#include "hydrometer.h"

namespace ferm {

bool updateDiacetylRest(uint32_t nowMs);
bool updateGradualCrash(uint32_t nowMs);
bool updateProgramRunner(uint32_t nowMs, const HydrometerReading &hydro);
void evaluateAlerts(uint32_t nowMs, const HydrometerReading &hydro);
// Sticky alert flags for status hint / attention badge (set by evaluateAlerts).
bool alertNotReachingActive();
bool alertLongRuntimeActive();
String buildHistoryRow(uint32_t nowMs, const HydrometerReading &hydro);

}  // namespace ferm