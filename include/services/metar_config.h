#pragma once

// User-editable center + radius for the METAR flight-category map. Persisted
// in NVS (Preferences namespace "metar"). Defaults keep the Bay Area
// envelope so the map looks the same as before the config was added.

namespace services::metar_config {

/** Load values from NVS. Falls back to config::kDefaultMetar* if unset. */
void init();

float centerLat();
float centerLon();
float radiusNm();

/** Parse and persist. Silently keeps prior values if either string doesn't
 *  scan as a finite lat/lon or the radius isn't a positive number. */
void saveFromStrings(const char* lat_str, const char* lon_str,
                     const char* radius_str);

}  // namespace services::metar_config
