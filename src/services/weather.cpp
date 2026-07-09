#include "services/weather.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#ifdef USE_NATIVE
#include <cstdio>
#include <string>
#else
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

namespace services::weather {

namespace {

// Bay Area primary field set for the weather-dot view. Coordinates from
// OurAirports (same source as the runway data). Order matters only for
// dot draw order — no need to prioritize.
Station s_stations[] = {
    // Bay Area primaries within a ~45 nm envelope of home. KAPC (Napa) and
    // KWVI (Watsonville) were dropped so the weather view can zoom tight
    // enough for the Peninsula/East Bay cluster to read cleanly.
    {"KSFO", 37.6188f, -122.3750f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KOAK", 37.7213f, -122.2214f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KSJC", 37.3639f, -121.9289f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KHWD", 37.6591f, -122.1214f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KLVK", 37.6934f, -121.8203f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KCCR", 37.9897f, -122.0567f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KHAF", 37.5136f, -122.5006f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KSQL", 37.5119f, -122.2495f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KPAO", 37.4611f, -122.1150f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KRHV", 37.3329f, -121.8195f, Category::Unknown, 0, 0, 0, INT32_MAX},
    {"KNUQ", 37.4161f, -122.0492f, Category::Unknown, 0, 0, 0, INT32_MAX},
};
constexpr size_t kStationCount = sizeof(s_stations) / sizeof(s_stations[0]);

unsigned long s_last_update_ms = 0;

// FAA flight-category rules — worst of ceiling or visibility drives the
// bucket. "Ceiling" is the LOWEST BKN/OVC layer; FEW/SCT don't count.
Category deriveCategory(int32_t ceiling_ft, int visibility_sm) {
  const bool no_ceiling = (ceiling_ft == INT32_MAX);
  auto ceilCat = [&]() -> Category {
    if (no_ceiling) return Category::VFR;
    if (ceiling_ft < 500)  return Category::LIFR;
    if (ceiling_ft < 1000) return Category::IFR;
    if (ceiling_ft <= 3000) return Category::MVFR;
    return Category::VFR;
  };
  auto visCat = [&]() -> Category {
    if (visibility_sm < 1) return Category::LIFR;
    if (visibility_sm < 3) return Category::IFR;
    if (visibility_sm <= 5) return Category::MVFR;
    return Category::VFR;
  };
  const Category c1 = ceilCat();
  const Category c2 = visCat();
  return (static_cast<uint8_t>(c1) > static_cast<uint8_t>(c2)) ? c1 : c2;
}

int parseVisibility(JsonVariantConst v) {
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    if (s && s[0]) {
      // "10+" → 10; "3" → 3; "2 1/2" → 2 (worst-case conservative).
      return std::atoi(s);
    }
    return 10;
  }
  if (v.is<int>() || v.is<float>() || v.is<double>()) {
    return static_cast<int>(v.as<float>());
  }
  return 10;
}

// Extracts the lowest BKN/OVC base as ceiling; anything else → INT32_MAX.
int32_t parseCeiling(JsonArrayConst clouds) {
  int32_t ceiling = INT32_MAX;
  for (JsonObjectConst layer : clouds) {
    const char* cover = layer["cover"].as<const char*>();
    if (!cover) continue;
    if (std::strcmp(cover, "BKN") == 0 || std::strcmp(cover, "OVC") == 0 ||
        std::strcmp(cover, "VV") == 0) {
      const int32_t base = layer["base"].as<int32_t>();
      if (base < ceiling) ceiling = base;
    }
  }
  return ceiling;
}

void applyMetar(Station& st, JsonObjectConst m) {
  st.wind_dir_deg   = m["wdir"].is<int>()     ? m["wdir"].as<int>()  : 0;
  st.wind_speed_kt  = m["wspd"].is<int>()     ? m["wspd"].as<int>()  : 0;
  st.visibility_sm  = parseVisibility(m["visib"]);
  st.ceiling_ft_agl = parseCeiling(m["clouds"].as<JsonArrayConst>());
  st.category       = deriveCategory(st.ceiling_ft_agl, st.visibility_sm);
}

void ingestPayload(const char* body, size_t body_len) {
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body, body_len);
  if (err) {
#ifdef USE_NATIVE
    std::printf("weather: json parse: %s\n", err.c_str());
#else
    Serial.printf("weather: json parse: %s\n", err.c_str());
#endif
    return;
  }
  JsonArrayConst arr = doc.as<JsonArrayConst>();
  for (JsonObjectConst m : arr) {
    const char* icao = m["icaoId"].as<const char*>();
    if (!icao) continue;
    for (auto& st : s_stations) {
      if (std::strcmp(st.icao, icao) == 0) {
        applyMetar(st, m);
        break;
      }
    }
  }
  s_last_update_ms =
#ifdef USE_NATIVE
      (unsigned long)millis();
#else
      millis();
#endif
}

// Build the CSV of ICAOs the API expects.
void buildIcaoList(char* out, size_t out_len) {
  out[0] = '\0';
  size_t used = 0;
  for (size_t i = 0; i < kStationCount; ++i) {
    const size_t need = std::strlen(s_stations[i].icao) + (i > 0 ? 1 : 0);
    if (used + need + 1 > out_len) break;
    if (i > 0) out[used++] = ',';
    std::memcpy(out + used, s_stations[i].icao,
                std::strlen(s_stations[i].icao));
    used += std::strlen(s_stations[i].icao);
    out[used] = '\0';
  }
}

}  // namespace

size_t stationCount() { return kStationCount; }
const Station* stations() { return s_stations; }
unsigned long lastUpdateMs() { return s_last_update_ms; }

bool update() {
  char ids[256];
  buildIcaoList(ids, sizeof(ids));

#ifdef USE_NATIVE
  char cmd[512];
  std::snprintf(cmd, sizeof(cmd),
                "curl -sf --max-time 8 "
                "'https://aviationweather.gov/api/data/metar?ids=%s&format=json'",
                ids);
  FILE* pipe = popen(cmd, "r");
  if (!pipe) return false;
  std::string body;
  body.reserve(16 * 1024);
  char buf[4096];
  while (size_t n = std::fread(buf, 1, sizeof(buf), pipe)) {
    body.append(buf, n);
  }
  const int rc = pclose(pipe);
  if (rc != 0 || body.empty()) {
    std::printf("weather: fetch failed rc=%d body=%zu\n", rc, body.size());
    return false;
  }
  ingestPayload(body.data(), body.size());
  return true;
#else
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  char url[512];
  std::snprintf(url, sizeof(url),
                "https://aviationweather.gov/api/data/metar?ids=%s&format=json",
                ids);
  if (!http.begin(client, url)) return false;
  http.setTimeout(8000);
  const int code = http.GET();
  if (code != 200) {
    Serial.printf("weather: http %d\n", code);
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();
  ingestPayload(payload.c_str(), payload.length());
  return true;
#endif
}

}  // namespace services::weather
