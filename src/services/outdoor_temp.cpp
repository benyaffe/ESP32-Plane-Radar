#include "services/outdoor_temp.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>

#ifdef USE_NATIVE
#include <cstdio>
#include <cstdlib>
#include <string>
#else
#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#endif

#include "services/radar_location.h"

namespace services::outdoor_temp {
namespace {

constexpr unsigned long kFetchIntervalMs = 15UL * 60UL * 1000UL;  // 15 min
constexpr unsigned long kFirstDelayMs    = 5UL * 1000UL;          // 5 s after boot

// hPa → inHg conversion for altimeter setting display.
constexpr float kHpaPerInHg = 33.8639f;

float s_temp_f = NAN;
float s_wind_kts = NAN;
float s_wind_deg = NAN;
float s_baro_inhg = NAN;
bool s_valid = false;
unsigned long s_last_fetch_ms = 0;
unsigned long s_last_attempt_ms = 0;
bool s_ever_attempted = false;
// UTC offset at the home lat/lon at fetch time. Populated from Open-
// Meteo's `utc_offset_seconds` — that field is only present when the
// URL includes `&timezone=auto`, which we now do. Cockpit uses this
// to render home-local HH:MM without embedding a lat→IANA table.
long s_utc_offset_sec = 0;
// Home lat/lon that the last successful fetch was made against. Used
// by loop() to detect a home move (WiFi portal / config server save)
// and force an immediate refetch so the clock's tz offset updates in
// seconds rather than waiting out the 15-minute TTL.
double s_last_fetch_lat = 0.0;
double s_last_fetch_lon = 0.0;

void buildUrl(char* url, size_t len) {
  std::snprintf(url, len,
                "http://api.open-meteo.com/v1/forecast?latitude=%.6f"
                "&longitude=%.6f"
                "&current=temperature_2m,wind_speed_10m,wind_direction_10m,pressure_msl"
                "&temperature_unit=fahrenheit&wind_speed_unit=kn"
                "&timezone=auto"
                "&forecast_days=1",
                services::location::homeLat(), services::location::homeLon());
}

bool ingestPayload(const char* body, size_t body_len) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body, body_len);
  if (err) {
#ifdef USE_NATIVE
    std::printf("outdoor_temp: json parse: %s\n", err.c_str());
#else
    Serial.printf("outdoor_temp: JSON parse error: %s\n", err.c_str());
#endif
    return false;
  }
  JsonVariant cur = doc["current"];
  if (!cur["temperature_2m"].is<float>() && !cur["temperature_2m"].is<int>()) {
#ifdef USE_NATIVE
    std::printf("outdoor_temp: missing temperature_2m\n");
#else
    Serial.println("outdoor_temp: missing temperature_2m");
#endif
    return false;
  }
  s_temp_f = cur["temperature_2m"].as<float>();
  // Wind + baro are best-effort — some Open-Meteo responses omit fields
  // near the coast. Fall back to NAN so the cockpit screen shows a "--"
  // placeholder without disabling the temperature display.
  s_wind_kts = (cur["wind_speed_10m"].is<float>() ||
                cur["wind_speed_10m"].is<int>())
                   ? cur["wind_speed_10m"].as<float>()
                   : NAN;
  s_wind_deg = (cur["wind_direction_10m"].is<float>() ||
                cur["wind_direction_10m"].is<int>())
                   ? cur["wind_direction_10m"].as<float>()
                   : NAN;
  if (cur["pressure_msl"].is<float>() || cur["pressure_msl"].is<int>()) {
    s_baro_inhg = cur["pressure_msl"].as<float>() / kHpaPerInHg;
  } else {
    s_baro_inhg = NAN;
  }
  // Home tz offset — top-level field, DST-aware for the fetch moment.
  // Keep the previous cached offset if the field is missing (e.g. API
  // returned an older schema); the stale value is still better than
  // reverting the clock to UTC mid-display.
  if (doc["utc_offset_seconds"].is<long>() ||
      doc["utc_offset_seconds"].is<int>()) {
    const long fresh_offset = doc["utc_offset_seconds"].as<long>();
    if (fresh_offset != s_utc_offset_sec) {
      s_utc_offset_sec = fresh_offset;
#if !defined(USE_NATIVE)
      // Persist so night_mode / cockpit have a valid tz immediately on
      // the next boot instead of running as UTC for the 5–30 s until
      // the first fetch lands. If the user moves home, the offset is
      // rewritten on the next successful fetch (loop() detects the
      // home move and refetches without waiting for the 15-min TTL).
      Preferences prefs;
      if (prefs.begin("wxcache", false)) {
        prefs.putLong("tz_offset", s_utc_offset_sec);
        prefs.end();
      }
#endif
    }
  }
  s_valid = true;
  s_last_fetch_ms = millis();
  s_last_fetch_lat = services::location::homeLat();
  s_last_fetch_lon = services::location::homeLon();
  return true;
}

#ifdef USE_NATIVE

bool doFetch() {
  char url[256];
  buildUrl(url, sizeof(url));
  char cmd[512];
  std::snprintf(cmd, sizeof(cmd), "curl -sf --max-time 8 '%s'", url);
  FILE* pipe = popen(cmd, "r");
  if (!pipe) return false;
  std::string body;
  body.reserve(4 * 1024);
  char buf[2048];
  while (size_t n = std::fread(buf, 1, sizeof(buf), pipe)) {
    body.append(buf, n);
  }
  const int rc = pclose(pipe);
  if (rc != 0 || body.empty()) {
    std::printf("outdoor_temp: fetch failed rc=%d body=%zu\n", rc, body.size());
    return false;
  }
  return ingestPayload(body.data(), body.size());
}

#else

bool doFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;
  char url[256];
  buildUrl(url, sizeof(url));

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.setTimeout(7000);
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("outdoor_temp: HTTP %d\n", code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  return ingestPayload(payload.c_str(), payload.length());
}

#endif

}  // namespace

void init() {
  s_temp_f = NAN;
  s_wind_kts = NAN;
  s_wind_deg = NAN;
  s_baro_inhg = NAN;
  s_valid = false;
  s_last_fetch_ms = 0;
  s_last_attempt_ms = 0;
  s_ever_attempted = false;
  s_utc_offset_sec = 0;
  s_last_fetch_lat = 0.0;
  s_last_fetch_lon = 0.0;
#if !defined(USE_NATIVE)
  // Seed the offset from NVS if a previous boot fetched it. Guards
  // night_mode / cockpit against reading UTC-based local time during
  // the first few seconds after boot (before Open-Meteo fetches).
  Preferences prefs;
  if (prefs.begin("wxcache", true)) {
    s_utc_offset_sec = prefs.getLong("tz_offset", 0);
    prefs.end();
  }
#endif
}

void loop() {
  const unsigned long now = millis();
  const unsigned long since_attempt = now - s_last_attempt_ms;
  const bool first = !s_ever_attempted;
  // Detect home moves (WiFi portal / emulator config save) — if home
  // has changed since our last successful fetch, force a refresh
  // regardless of the TTL so the cockpit's tz offset updates in seconds
  // rather than waiting out the 15-minute cadence. Anchored on home (not
  // the focus-override lat/lon) so focus cycling doesn't re-fetch OAT.
  const bool home_moved =
      s_valid &&
      (services::location::homeLat() != s_last_fetch_lat ||
       services::location::homeLon() != s_last_fetch_lon);
  const bool ok_to_retry = home_moved
                               ? true
                               : (s_valid ? (since_attempt >= kFetchIntervalMs)
                                          : (since_attempt >= 30000UL));
  if (first && now < kFirstDelayMs) return;
  if (!first && !ok_to_retry) return;
  s_last_attempt_ms = now;
  s_ever_attempted = true;
  doFetch();
}

Reading cached() {
  Reading r;
  r.tempF = s_temp_f;
  r.windKts = s_wind_kts;
  r.windDegFrom = s_wind_deg;
  r.baroInHg = s_baro_inhg;
  r.valid = s_valid;
  r.age_ms = s_last_fetch_ms == 0 ? 0 : (millis() - s_last_fetch_ms);
  r.utcOffsetSec = s_utc_offset_sec;
  return r;
}

// Public thunk into the anon-namespace parser. Anon-namespace members
// are visible to the whole TU via unqualified lookup so this call
// resolves to the ingestPayload defined higher up in the file.
bool ingestPayloadForTest(const char* body, unsigned long body_len) {
  return ingestPayload(body, static_cast<size_t>(body_len));
}

}  // namespace services::outdoor_temp
