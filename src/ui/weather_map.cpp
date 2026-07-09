#include "ui/weather_map.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/radar_location.h"
#include "services/weather.h"
#include "ui/radar_theme.h"

namespace ui::weather {
namespace {

// Weather-map projection: centered on the current focus/home, 120 km
// radius (~65 nm) so every Bay Area primary field lands on-screen. The
// 105-px projection radius leaves 15 px of margin for labels + legend
// inside the physical bezel (120 px).
constexpr float kWeatherRadiusKm  = 120.0f;
constexpr int   kProjectionPx     = 105;
constexpr float kKmPerDeg         = 111.0f;

uint16_t categoryColor(services::weather::Category c) {
  switch (c) {
    case services::weather::Category::VFR:   return tft.color565( 40, 200,  60);
    case services::weather::Category::MVFR:  return tft.color565( 70, 130, 255);
    case services::weather::Category::IFR:   return tft.color565(240,  70,  70);
    case services::weather::Category::LIFR:  return tft.color565(220,  70, 200);
    default:                                 return tft.color565(120, 120, 120);
  }
}

const char* categoryLabel(services::weather::Category c) {
  switch (c) {
    case services::weather::Category::VFR:  return "VFR";
    case services::weather::Category::MVFR: return "MVFR";
    case services::weather::Category::IFR:  return "IFR";
    case services::weather::Category::LIFR: return "LIFR";
    default:                                return "?";
  }
}

void projectLatLon(float lat, float lon, int* out_x, int* out_y) {
  const double center_lat = services::location::lat();
  const double center_lon = services::location::lon();
  const float px_per_km   = static_cast<float>(kProjectionPx) / kWeatherRadiusKm;
  const float dx_km = static_cast<float>(lon - center_lon) * kKmPerDeg;
  const float dy_km = static_cast<float>(lat - center_lat) * kKmPerDeg;
  *out_x = radar::kCenterX + static_cast<int>(std::lroundf(dx_km * px_per_km));
  *out_y = radar::kCenterY - static_cast<int>(std::lroundf(dy_km * px_per_km));
}

void drawTitle(lgfx::LGFXBase& gfx) {
  displayFontEnsureLoaded(gfx);
  gfx.setTextSize(0.42f);
  gfx.setTextColor(radar::kColorLabel, radar::kColorBackground);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.drawString("WX", radar::kCenterX, 8);
}

// Small legend row across the middle of the bezel margin at the bottom.
// Four color chips + text: "VFR MVFR IFR LIFR".
void drawLegend(lgfx::LGFXBase& gfx) {
  constexpr int y = 220;
  constexpr int chip = 4;
  const services::weather::Category cats[] = {
      services::weather::Category::VFR,
      services::weather::Category::MVFR,
      services::weather::Category::IFR,
      services::weather::Category::LIFR,
  };
  const char* labels[] = {"V", "M", "I", "L"};
  // Layout: 4 pairs, each ~24 px wide, spread across ~96 px centered on cx.
  int x = radar::kCenterX - 44;
  gfx.setTextSize(0.36f);
  gfx.setTextDatum(textdatum_t::middle_left);
  for (int i = 0; i < 4; ++i) {
    gfx.fillCircle(x, y, chip, categoryColor(cats[i]));
    gfx.setTextColor(radar::kColorLabel, radar::kColorBackground);
    gfx.drawString(labels[i], x + chip + 2, y);
    x += 24;
  }
}

void drawStations(lgfx::LGFXBase& gfx) {
  const services::weather::Station* stations = services::weather::stations();
  const size_t n = services::weather::stationCount();
  displayFontEnsureLoaded(gfx);
  gfx.setTextSize(0.38f);
  gfx.setTextDatum(textdatum_t::top_center);

  for (size_t i = 0; i < n; ++i) {
    int sx = 0, sy = 0;
    projectLatLon(stations[i].lat, stations[i].lon, &sx, &sy);
    // Off-projection skip.
    const int dx = sx - radar::kCenterX;
    const int dy = sy - radar::kCenterY;
    if (dx * dx + dy * dy > kProjectionPx * kProjectionPx) continue;

    const uint16_t color = categoryColor(stations[i].category);
    gfx.fillCircle(sx, sy, 5, color);
    gfx.drawCircle(sx, sy, 5, radar::kColorBackground);  // subtle outline

    // ICAO label above the dot (drop the leading K).
    const char* id = stations[i].icao;
    if (id[0] == 'K' && id[1]) id++;
    gfx.setTextColor(radar::kColorLabel, radar::kColorBackground);
    gfx.drawString(id, sx, sy + 8);
  }
}

void drawFreshness(lgfx::LGFXBase& gfx) {
  const unsigned long last = services::weather::lastUpdateMs();
  gfx.setTextSize(0.34f);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setTextColor(radar::kColorGrid, radar::kColorBackground);
  char buf[24];
  if (last == 0) {
    std::strncpy(buf, "no data", sizeof(buf));
  } else {
#ifdef USE_NATIVE
    const unsigned long age_s = (millis() - last) / 1000;
#else
    const unsigned long age_s = (millis() - last) / 1000;
#endif
    if (age_s < 60) std::snprintf(buf, sizeof(buf), "%lus ago", age_s);
    else            std::snprintf(buf, sizeof(buf), "%lum ago", age_s / 60);
  }
  gfx.drawString(buf, radar::kCenterX, 22);
}

}  // namespace

void refresh() {
  // Cache for 5 min — METARs update ~hourly, but SPECIALs can appear
  // any time and the extra freshness is cheap.
  constexpr unsigned long kTtlMs = 5UL * 60UL * 1000UL;
  const unsigned long last = services::weather::lastUpdateMs();
  const unsigned long now  = millis();
  if (last == 0 || (now - last) >= kTtlMs) {
    services::weather::update();
  }
}

void draw() {
  tft.fillScreen(radar::kColorBackground);
  drawTitle(tft);
  drawFreshness(tft);
  drawStations(tft);
  drawLegend(tft);
  // Bezel mask — same as the radar view. Cheap redundancy; keeps SDL
  // visually matched to the round physical panel.
  tft.fillArc(radar::kCenterX, radar::kCenterY, radar::kSize + 8, 120, 0, 360,
              radar::kColorBackground);
}

}  // namespace ui::weather
