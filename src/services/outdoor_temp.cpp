#include "services/outdoor_temp.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>

#ifdef USE_NATIVE
#include <cstdlib>
#include <string>
#else
#include <Arduino.h>
#include <HTTPClient.h>
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

#ifndef USE_NATIVE

bool doFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[256];
  std::snprintf(url, sizeof(url),
                "http://api.open-meteo.com/v1/forecast?latitude=%.6f"
                "&longitude=%.6f"
                "&current=temperature_2m,wind_speed_10m,wind_direction_10m,pressure_msl"
                "&temperature_unit=fahrenheit&wind_speed_unit=kn"
                "&forecast_days=1",
                services::location::lat(), services::location::lon());

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

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("outdoor_temp: JSON parse error: %s\n", err.c_str());
    return false;
  }
  JsonVariant cur = doc["current"];
  if (!cur["temperature_2m"].is<float>() && !cur["temperature_2m"].is<int>()) {
    Serial.println("outdoor_temp: missing temperature_2m");
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
  s_valid = true;
  s_last_fetch_ms = millis();
  Serial.printf("outdoor_temp: %.1fF, wind %.0f@%.0fkt, baro %.2f inHg\n",
                s_temp_f, s_wind_deg, s_wind_kts, s_baro_inhg);
  return true;
}

#endif

}  // namespace

void init() {
#ifdef USE_NATIVE
  // Native emulator has no network access — seed the cache with plausible
  // SF Bay Area values immediately so the cockpit screen shows a full
  // instrument set the moment it opens. Real hardware overrides these
  // when the first Open-Meteo fetch lands.
  s_temp_f = 61.0f;
  s_wind_kts = 12.0f;
  s_wind_deg = 280.0f;
  s_baro_inhg = 29.92f;
  s_valid = true;
  s_last_fetch_ms = 1;  // non-zero so age_ms() returns something sensible
#else
  s_temp_f = NAN;
  s_wind_kts = NAN;
  s_wind_deg = NAN;
  s_baro_inhg = NAN;
  s_valid = false;
  s_last_fetch_ms = 0;
#endif
  s_last_attempt_ms = 0;
  s_ever_attempted = false;
}

void loop() {
#ifdef USE_NATIVE
  // Native builds get canned readings a few seconds after boot so the
  // cockpit screen shows a full instrument set without network access.
  if (!s_valid && millis() > kFirstDelayMs) {
    s_temp_f = 61.0f;     // typical SF morning
    s_wind_kts = 12.0f;   // typical SF afternoon westerly
    s_wind_deg = 280.0f;  // WNW
    s_baro_inhg = 29.92f; // ISA standard
    s_valid = true;
    s_last_fetch_ms = millis();
  }
#else
  const unsigned long now = millis();
  const unsigned long since_attempt = now - s_last_attempt_ms;
  const bool first = !s_ever_attempted;
  const bool ok_to_retry = s_valid ? (since_attempt >= kFetchIntervalMs)
                                   : (since_attempt >= 30000UL);
  if (first && now < kFirstDelayMs) return;
  if (!first && !ok_to_retry) return;
  s_last_attempt_ms = now;
  s_ever_attempted = true;
  doFetch();
#endif
}

Reading cached() {
  Reading r;
  r.tempF = s_temp_f;
  r.windKts = s_wind_kts;
  r.windDegFrom = s_wind_deg;
  r.baroInHg = s_baro_inhg;
  r.valid = s_valid;
  r.age_ms = s_last_fetch_ms == 0 ? 0 : (millis() - s_last_fetch_ms);
  return r;
}

}  // namespace services::outdoor_temp
