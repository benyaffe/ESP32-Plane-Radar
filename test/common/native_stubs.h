// Minimal linker stubs for symbols that map_projection.cpp references but
// unit tests don't exercise.
//
// USAGE: exactly ONE .cpp per test binary must do:
//     #define NATIVE_STUBS_DEFINE
//     #include "../common/native_stubs.h"
// Other .cpp files (if any) may include the header without the define
// to get the declarations only.
//
// The header-only approach with `inline` doesn't work: map_projection.cpp
// references these symbols from a different translation unit, and inline
// definitions are only emitted where called from.

#pragma once

#include "ui/radar_range.h"

namespace services::location {
double lat();
double lon();
}  // namespace services::location

namespace ui::radar {
const RangePreset& rangeCurrent();
}

#ifdef NATIVE_STUBS_DEFINE
namespace services::location {
double lat() { return 37.7552; }
double lon() { return -122.4528; }
}  // namespace services::location

namespace ui::radar {
const RangePreset& rangeCurrent() {
  static const RangePreset kPreset = kRangePresets[1];  // 10 nm
  return kPreset;
}
}  // namespace ui::radar
#endif
