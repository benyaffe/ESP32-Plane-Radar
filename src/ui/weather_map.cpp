#include "ui/weather_map.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "data/coastlines.h"
#include "data/land.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/radar_location.h"
#include "services/weather.h"
#include "ui/radar_theme.h"

namespace ui::weather {
namespace {

// Weather-map projection: centered on the current focus/home, ~48 nm
// radius so KCCR (~13 nm N) to KRHV (~35 nm SE) fit with margin without
// crowding out the tight Peninsula cluster. The 108-px projection radius
// leaves 12 px of margin inside the physical bezel (120 px) for labels
// + legend.
constexpr float kWeatherRadiusKm  = 88.0f;
constexpr int   kProjectionPx     = 108;
constexpr float kKmPerDeg         = 111.0f;
constexpr float kE7               = 1e-7f;

uint16_t categoryColor(services::weather::Category c) {
  switch (c) {
    case services::weather::Category::VFR:   return tft.color565( 40, 200,  60);
    case services::weather::Category::MVFR:  return tft.color565( 70, 130, 255);
    case services::weather::Category::IFR:   return tft.color565(240,  70,  70);
    case services::weather::Category::LIFR:  return tft.color565(220,  70, 200);
    default:                                 return tft.color565(120, 120, 120);
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

bool insideDisc(int x, int y) {
  const int dx = x - radar::kCenterX;
  const int dy = y - radar::kCenterY;
  return dx * dx + dy * dy <= kProjectionPx * kProjectionPx;
}

// Land: iterate the baked triangles and fill them at weather zoom. Each
// triangle spans three vertices already in the baked cache; we just
// re-project them here. Any triangle entirely outside the projection
// disc is dropped early. Triangles clipping the disc still draw and
// spill past the boundary — the bezel mask at the end catches the
// overflow.
void drawLand(lgfx::LGFXBase& gfx) {
  const uint16_t color = radar::kColorLand;
  for (size_t i = 0; i < data::land::kTriangleCount; ++i) {
    const data::land::Triangle& t = data::land::kTriangles[i];
    const data::land::Vertex& v0 = data::land::kVertices[t.v0];
    const data::land::Vertex& v1 = data::land::kVertices[t.v1];
    const data::land::Vertex& v2 = data::land::kVertices[t.v2];
    int x0, y0, x1, y1, x2, y2;
    projectLatLon(v0.lat_e7 * kE7, v0.lon_e7 * kE7, &x0, &y0);
    projectLatLon(v1.lat_e7 * kE7, v1.lon_e7 * kE7, &x1, &y1);
    projectLatLon(v2.lat_e7 * kE7, v2.lon_e7 * kE7, &x2, &y2);
    if (!insideDisc(x0, y0) && !insideDisc(x1, y1) && !insideDisc(x2, y2)) {
      continue;
    }
    gfx.fillTriangle(x0, y0, x1, y1, x2, y2, color);
  }
}

// Coastline: iterate polylines, quick-reject those wholly outside the
// disc, drawLine the survivors segment-by-segment. Simpler than a
// full-clip solution and looks fine at this zoom because coastline is
// dense (Peninsula, East Bay, Marin all present).
void drawCoast(lgfx::LGFXBase& gfx) {
  const uint16_t color = tft.color565(radar::kBgR + 40, radar::kBgG + 60,
                                      radar::kBgB + 40);
  for (size_t i = 0; i < data::coastlines::kPolylineCount; ++i) {
    const data::coastlines::Polyline& pl = data::coastlines::kPolylines[i];
    for (uint16_t k = 1; k < pl.count; ++k) {
      const data::coastlines::Point& a = data::coastlines::kPoints[pl.start + k - 1];
      const data::coastlines::Point& b = data::coastlines::kPoints[pl.start + k];
      int ax, ay, bx, by;
      projectLatLon(a.lat_e7 * kE7, a.lon_e7 * kE7, &ax, &ay);
      projectLatLon(b.lat_e7 * kE7, b.lon_e7 * kE7, &bx, &by);
      if (!insideDisc(ax, ay) && !insideDisc(bx, by)) continue;
      gfx.drawLine(ax, ay, bx, by, color);
    }
  }
}

void drawTitle(lgfx::LGFXBase& gfx) {
  displayFontEnsureLoaded(gfx);
  gfx.setTextSize(0.44f);
  gfx.setTextColor(radar::kColorLabel, radar::kColorBackground);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.drawString("WX", radar::kCenterX, 6);
}

// Small legend row across the middle of the bezel margin at the bottom.
// Four color chips + text.
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
  int x = radar::kCenterX - 44;
  gfx.setTextSize(0.40f);
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
  gfx.setTextSize(0.44f);
  gfx.setTextDatum(textdatum_t::top_center);

  for (size_t i = 0; i < n; ++i) {
    int sx = 0, sy = 0;
    projectLatLon(stations[i].lat, stations[i].lon, &sx, &sy);
    if (!insideDisc(sx, sy)) continue;

    const uint16_t color = categoryColor(stations[i].category);
    gfx.fillCircle(sx, sy, 4, color);
    gfx.drawCircle(sx, sy, 4, radar::kColorBackground);  // subtle outline

    // ICAO label above the dot (drop the leading K).
    const char* id = stations[i].icao;
    if (id[0] == 'K' && id[1]) id++;
    gfx.setTextColor(radar::kColorLabel, radar::kColorBackground);
    gfx.drawString(id, sx, sy + 7);
  }
}

void drawFreshness(lgfx::LGFXBase& gfx) {
  const unsigned long last = services::weather::lastUpdateMs();
  gfx.setTextSize(0.36f);
  gfx.setTextDatum(textdatum_t::top_center);
  gfx.setTextColor(radar::kColorGrid, radar::kColorBackground);
  char buf[24];
  if (last == 0) {
    std::strncpy(buf, "no data", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
  } else {
    const unsigned long age_s = (millis() - last) / 1000;
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
  drawLand(tft);
  drawCoast(tft);
  drawTitle(tft);
  drawFreshness(tft);
  drawStations(tft);
  drawLegend(tft);
  // Bezel mask — same as the radar view. Keeps SDL visually matched to
  // the round physical panel.
  tft.fillArc(radar::kCenterX, radar::kCenterY, radar::kSize + 8, 120, 0, 360,
              radar::kColorBackground);
}

}  // namespace ui::weather
