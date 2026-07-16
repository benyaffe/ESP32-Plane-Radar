#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {

// adsb.fi labels every track with a receiver-source string in its JSON
// "type" field. We map it into this enum on ingestion and use it (via
// confidence()) to decide which of two co-located tracks to keep when
// deduping ghost aircraft (see deduplicateGhosts). Any unrecognized
// string becomes Unknown — never crash on a new source name.
enum class AdsbSource : uint8_t {
  Unknown = 0,       // string absent or not recognized → tier 0
  AdsbIcao,          // direct ADS-B from an ICAO-registered aircraft — tier 3
  AdsbIcaoNt,        // direct ADS-B, non-transponder — tier 3
  AdsrIcao,          // ADS-R rebroadcast of an ADS-B aircraft — tier 2
  TisbIcao,          // TIS-B rebroadcast of a Mode-S / ADS-B aircraft — tier 2
  Mlat,              // position computed by multilateration — tier 1
  TisbTrackfile,     // TIS-B track-file entry — tier 0
  TisbOther,         // other TIS-B variant — tier 0
  AdsbOther,         // ADS-B from a non-ICAO source (~-prefixed) — tier 0
  ModeS,             // Mode-S only, no position — tier 0
  Adsc,              // ADS-C (rare, mostly oceanic) — tier 0
};

/** Parse adsb.fi's "type" string into an AdsbSource. Unknown values map
 *  to AdsbSource::Unknown. */
AdsbSource parseSource(const char* s);

/** Confidence tier 0..3 for a source — higher = more trustworthy. Used
 *  by deduplicateGhosts to decide which track to drop in a cross-tier
 *  ghost pair. */
uint8_t confidence(AdsbSource s);

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  float vs_fpm;   // Barometric or geometric vertical rate, ft/min. 0 if unknown.
  int32_t alt_ft; // Integer altitude in feet, or INT32_MIN for on-ground / unknown.
  uint16_t squawk; // Transponder code as a 4-digit decimal (e.g. 7700). 0 if unknown.
  char callsign[9];
  char type[5];
  char reg[9];       // Registration / tail number (adsb.fi "r"). Used only
                     // by the ghost-dedup identity guard.
  AdsbSource source; // Receiver-source tier for ghost dedup.
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Time (millis()) of the most recent successful fetchUpdate. Returns 0 if
 *  none yet. */
unsigned long lastUpdateMs();

/** Monotonically increasing counter of successful fetches. Tag rendering
 *  uses this to alternate the second-line mode once per fetch (so each
 *  mode gets one full fetch window and the mode swap coincides with the
 *  position update rather than fighting it). */
unsigned long fetchCount();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/** Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi. */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

/** Parse an adsb.fi JSON body directly into the cached aircraft list,
 *  bypassing HTTP. Returns false + leaves the list alone on parse errors.
 *  Exposed for tests — production callers use fetchUpdate(). */
bool ingestPayloadForTest(const char* body, unsigned long body_len);

/** Walk `list` (size *n) and drop lower-tier tracks whose geometry AND
 *  identity match a higher-tier neighbor (a "ghost" — same physical plane
 *  reported twice via different receiver channels). The array is compacted
 *  in place; *n is decreased by the number of drops. Both fetchUpdate and
 *  the native host_stubs equivalent call this after populating the list. */
void deduplicateGhosts(Aircraft* list, size_t* n);

}  // namespace services::adsb
