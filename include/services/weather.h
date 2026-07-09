#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>

// METAR-driven weather snapshot for a fixed list of Bay Area airports.
// Fetched in one bulk call from aviationweather.gov (public, no key).
// Flight category is computed locally from ceiling + visibility per FAA
// rules — the API's `fltcat` field is currently empty in responses.

namespace services::weather {

enum class Category : uint8_t {
  Unknown,
  VFR,   // ceiling > 3000 ft AGL and vis > 5 sm
  MVFR,  // ceiling 1000-3000 ft or vis 3-5 sm
  IFR,   // ceiling 500-1000 ft or vis 1-3 sm
  LIFR,  // ceiling < 500 ft or vis < 1 sm
};

struct Station {
  const char* icao;      // "KSFO"
  float       lat;       // deg
  float       lon;       // deg
  Category    category;
  int16_t     wind_dir_deg;    // 0 if calm / variable / unknown
  int16_t     wind_speed_kt;
  int16_t     visibility_sm;   // 10 for "10+"
  int32_t     ceiling_ft_agl;  // INT32_MAX = no ceiling reported
};

/** Fixed list of stations we render on the weather-dot view. */
size_t stationCount();
const Station* stations();

/** Fetch fresh METARs for all stations. Returns true on success. Safe
 *  to call from anywhere in the main loop (blocks on HTTP). */
bool update();

/** millis() of the most recent successful update, or 0 if none yet. */
unsigned long lastUpdateMs();

}  // namespace services::weather
