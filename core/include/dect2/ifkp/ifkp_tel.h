// IFKP receiver telemetry, shared by the receiver and the UI (the common part is Kbd33Telemetry, kbd33_station.h).
#pragma once
#include "dect2/fsq/kbd33_station.h"

namespace dect2 {

struct IfkpTelemetry : Kbd33Telemetry {};

inline std::string ifkpSummary(const IfkpTelemetry& t) { return kbd33Summary(t, "IFKP"); }

} // namespace dect2
