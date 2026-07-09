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

float s_temp_f = NAN;
bool s_valid = false;
unsigned long s_last_fetch_ms = 0;
unsigned long s_last_attempt_ms = 0;
bool s_ever_attempted = false;

#ifndef USE_NATIVE

bool doFetch() {
  if (WiFi.status() != WL_CONNECTED) return false;

  char url[192];
  std::snprintf(url, sizeof(url),
                "http://api.open-meteo.com/v1/forecast?latitude=%.6f"
                "&longitude=%.6f"
                "&current=temperature_2m&temperature_unit=fahrenheit"
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
  if (!doc["current"]["temperature_2m"].is<float>() &&
      !doc["current"]["temperature_2m"].is<double>() &&
      !doc["current"]["temperature_2m"].is<int>()) {
    Serial.println("outdoor_temp: missing temperature_2m");
    return false;
  }
  s_temp_f = doc["current"]["temperature_2m"].as<float>();
  s_valid = true;
  s_last_fetch_ms = millis();
  Serial.printf("outdoor_temp: %.1f F\n", s_temp_f);
  return true;
}

#endif

}  // namespace

void init() {
  s_temp_f = NAN;
  s_valid = false;
  s_last_fetch_ms = 0;
  s_last_attempt_ms = 0;
  s_ever_attempted = false;
}

void loop() {
#ifdef USE_NATIVE
  // Native builds get a fake reading a few seconds after boot so the
  // cockpit screen has something to show without needing network access.
  if (!s_valid && millis() > kFirstDelayMs) {
    s_temp_f = 61.0f;  // arbitrary "SF morning" placeholder
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
  r.valid = s_valid;
  r.age_ms = s_last_fetch_ms == 0 ? 0 : (millis() - s_last_fetch_ms);
  return r;
}

}  // namespace services::outdoor_temp
