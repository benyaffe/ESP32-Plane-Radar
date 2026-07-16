// Ghost-aircraft deduplication.
//
// adsb.fi's feed frequently reports the same physical plane twice: once
// via direct ADS-B and once via a rebroadcast channel (TIS-B / ADS-R /
// MLAT). The rebroadcast track lags the primary by a few seconds and
// often has a synthetic hex id and no callsign, so on screen it looks
// like a second aircraft parked on top of the first.
//
// Approach: after ingestion, walk the aircraft list and drop any track
// that is (a) at a strictly lower confidence tier than a nearby
// neighbor and (b) close enough on ALL of {lateral, vertical, track,
// speed} and (c) either has no identity at all or shares its
// callsign/registration with the higher-tier neighbor.
//
// This file is kept separate from adsb_client.cpp so both the ESP32
// (supermini) and the desktop SDL emulator (native) can link the same
// implementation — adsb_client.cpp itself is excluded from the native
// build (it depends on HTTPClient / WiFiClientSecure).

#include "services/adsb_client.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

namespace services::adsb {

AdsbSource parseSource(const char* s) {
  if (s == nullptr || s[0] == '\0') return AdsbSource::Unknown;
  // Ordered by frequency in the deep-validation sample (91 % adsb_icao).
  if (std::strcmp(s, "adsb_icao") == 0)      return AdsbSource::AdsbIcao;
  if (std::strcmp(s, "adsr_icao") == 0)      return AdsbSource::AdsrIcao;
  if (std::strcmp(s, "adsb_icao_nt") == 0)   return AdsbSource::AdsbIcaoNt;
  if (std::strcmp(s, "mlat") == 0)           return AdsbSource::Mlat;
  if (std::strcmp(s, "tisb_icao") == 0)      return AdsbSource::TisbIcao;
  if (std::strcmp(s, "tisb_other") == 0)     return AdsbSource::TisbOther;
  if (std::strcmp(s, "tisb_trackfile") == 0) return AdsbSource::TisbTrackfile;
  if (std::strcmp(s, "adsb_other") == 0)     return AdsbSource::AdsbOther;
  if (std::strcmp(s, "mode_s") == 0)         return AdsbSource::ModeS;
  if (std::strcmp(s, "adsc") == 0)           return AdsbSource::Adsc;
  return AdsbSource::Unknown;
}

uint8_t confidence(AdsbSource s) {
  switch (s) {
    case AdsbSource::AdsbIcao:
    case AdsbSource::AdsbIcaoNt:
      return 3;
    case AdsbSource::AdsrIcao:
    case AdsbSource::TisbIcao:
      return 2;
    case AdsbSource::Mlat:
      return 1;
    default:
      return 0;
  }
}

namespace {

// Case-insensitive equal, treating a leading '~' as absent so adsb.fi's
// non-ICAO prefix doesn't break identity matches. Both inputs empty →
// false: two empties aren't a match, they're an "unknown vs unknown" pair
// which the caller handles as its own case.
bool identsMatch(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  if (a[0] == '~') ++a;
  if (b[0] == '~') ++b;
  if (a[0] == '\0' || b[0] == '\0') return false;
  while (*a && *b) {
    const char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a;
    const char cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
    if (ca != cb) return false;
    ++a; ++b;
  }
  return *a == '\0' && *b == '\0';
}

// Great-circle is overkill at 0.25 nm; a flat lat/lon → meters
// approximation is <1 % off at any latitude and much cheaper.
float approxDistMeters(float lat1, float lon1, float lat2, float lon2) {
  constexpr float kMetersPerDegLat = 111320.0f;
  const float mid_lat_rad = (lat1 + lat2) * 0.5f * 0.01745329252f;  // π/180
  const float mx = (lon2 - lon1) * kMetersPerDegLat * std::cos(mid_lat_rad);
  const float my = (lat2 - lat1) * kMetersPerDegLat;
  return std::sqrt(mx * mx + my * my);
}

// All four geometry thresholds must hold. Missing track/gs skips that
// dimension rather than spuriously failing (adsb.fi omits them for some
// tracks; treating an absent field as "infinitely different" would
// mask real ghosts).
bool geometryLooksLikeGhost(const Aircraft& a, const Aircraft& b) {
  constexpr float kMaxLateralMeters = 460.0f;   // ~0.25 nm
  constexpr int32_t kMaxAltDiffFt = 300;
  constexpr float kMaxTrackDiffDeg = 30.0f;
  constexpr float kMaxSpeedRatio = 0.30f;

  if (approxDistMeters(a.lat, a.lon, b.lat, b.lon) > kMaxLateralMeters) {
    return false;
  }
  if (a.alt_ft != INT32_MIN && b.alt_ft != INT32_MIN) {
    if (std::abs(a.alt_ft - b.alt_ft) > kMaxAltDiffFt) return false;
  }
  if (a.track_deg > 0.0f && b.track_deg > 0.0f) {
    float dt = std::fabs(a.track_deg - b.track_deg);
    if (dt > 180.0f) dt = 360.0f - dt;
    if (dt > kMaxTrackDiffDeg) return false;
  }
  if (a.gs_knots > 0.0f && b.gs_knots > 0.0f) {
    const float bigger = std::max(a.gs_knots, b.gs_knots);
    if (std::fabs(a.gs_knots - b.gs_knots) / bigger > kMaxSpeedRatio) {
      return false;
    }
  }
  return true;
}

// The identity guard is CRITICAL — live validation showed the geometry
// gate alone would drop ~100 legitimate aircraft flying in close
// formation for every 16 real ghost pairs. Only drop the low-tier track
// if it has no identity at all, OR its identity matches the high-tier
// candidate.
bool identityGuardPasses(const Aircraft& low, const Aircraft& high) {
  const bool low_has_id = low.callsign[0] != '\0' || low.reg[0] != '\0';
  if (!low_has_id) return true;  // classic no-ID ghost — trust geometry
  if (identsMatch(low.callsign, high.callsign)) return true;
  if (identsMatch(low.reg, high.reg)) return true;
  return false;
}

}  // namespace

void deduplicateGhosts(Aircraft* list, size_t* n) {
  if (n == nullptr || *n < 2) return;
  bool drop[kMaxAircraft] = {};
  for (size_t i = 0; i < *n; ++i) {
    if (drop[i]) continue;
    for (size_t j = i + 1; j < *n; ++j) {
      if (drop[j]) continue;
      const uint8_t ti = confidence(list[i].source);
      const uint8_t tj = confidence(list[j].source);
      if (ti == tj) continue;  // same-tier neighbors both survive
      if (!geometryLooksLikeGhost(list[i], list[j])) continue;
      const size_t low_idx = (ti < tj) ? i : j;
      const size_t high_idx = (ti < tj) ? j : i;
      if (identityGuardPasses(list[low_idx], list[high_idx])) {
        drop[low_idx] = true;
        if (low_idx == i) break;  // i is gone — move to next i
      }
    }
  }
  size_t w = 0;
  for (size_t r = 0; r < *n; ++r) {
    if (!drop[r]) {
      if (w != r) list[w] = list[r];
      ++w;
    }
  }
  *n = w;
}

}  // namespace services::adsb
