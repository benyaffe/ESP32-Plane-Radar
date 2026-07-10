#pragma once

#include <cstdint>

#include "services/weather.h"

namespace services::weather {

// FAA flight-category rule: the worst of the ceiling bucket and the
// visibility bucket wins. Ceiling is the LOWEST BKN/OVC/VV layer; FEW/SCT
// don't count. Sentinel: ceiling_ft == INT32_MAX means "no reported ceiling".
Category deriveCategory(int32_t ceiling_ft, int visibility_sm);

}  // namespace services::weather
