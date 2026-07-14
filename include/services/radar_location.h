#pragma once

namespace services::location {

/** Load saved lat/lon from NVS, or use config defaults. Call once before WiFi setup. */
void init();

/** Currently-active radar center. Honors any focus-point override set via
 *  setOverride() — the radar/ADS-B/weather-map viewports all steer off this
 *  so cycling focus recenters the map. */
double lat();
double lon();

/** Persisted user home, ignoring any focus override. Cockpit / outdoor-temp
 *  fetch anchor to home so the clock's tz, OAT, wind, baro, and the "N nm N
 *  of KSFO" reference-position line all stay pinned to home regardless of
 *  which focus airport the radar is looking at. */
double homeLat();
double homeLon();

/** Parse portal strings, validate, persist to NVS, update runtime values. */
bool saveFromStrings(const char* lat_str, const char* lon_str);

/** Clear stored coordinates (e.g. with WiFi credential reset). */
void clear();

/** Focus-point override: when set, lat()/lon() return these instead of the
 *  stored home. homeLat()/homeLon() are unaffected. Not persisted. */
void setOverride(double lat, double lon);
void clearOverride();

}  // namespace services::location
