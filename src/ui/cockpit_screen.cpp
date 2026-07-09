#include "ui/cockpit_screen.h"

#include <cmath>
#include <cstdio>
#include <ctime>

#include "hardware/display.h"
#include "services/env_sensor.h"
#include "services/outdoor_temp.h"
#include "ui/radar_display.h"
#include "ui/radar_theme.h"

namespace ui::cockpit {
namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Colors picked to read well on the same dark radar background so the
// display doesn't visually re-adjust between screens. Slightly warmer
// whites/grays than pure #FFFFFF/#7BEF for a "instrument panel" feel.
// Green matches typical glass-cockpit (Garmin G1000 / PFD) legend text.
uint16_t colWhite() { return tft.color565(230, 232, 235); }
uint16_t colGray()  { return tft.color565( 96,  96, 104); }
uint16_t colAmber() { return tft.color565(255, 190,  40); }
uint16_t colTemp()  { return tft.color565(180, 200, 230); }
uint16_t colGreen() { return tft.color565( 80, 220,  80); }
uint16_t colFrame() { return tft.color565( 60,  90,  60); }

void drawRadialLine(LGFX_Sprite& g, float angle_rad, int inner, int outer,
                    uint16_t color) {
  const float cx = static_cast<float>(radar::kCenterX);
  const float cy = static_cast<float>(radar::kCenterY);
  const float cs = std::cos(angle_rad);
  const float sn = std::sin(angle_rad);
  const int x0 = static_cast<int>(std::lroundf(cx + cs * inner));
  const int y0 = static_cast<int>(std::lroundf(cy + sn * inner));
  const int x1 = static_cast<int>(std::lroundf(cx + cs * outer));
  const int y1 = static_cast<int>(std::lroundf(cy + sn * outer));
  g.drawLine(x0, y0, x1, y1, color);
}

void drawTicks(LGFX_Sprite& g) {
  for (int i = 0; i < 60; ++i) {
    const float angle = (static_cast<float>(i) * 6.0f - 90.0f) * kDegToRad;
    if (i % 5 == 0) {
      drawRadialLine(g, angle, 92, 108, colWhite());
    } else {
      drawRadialLine(g, angle, 92, 100, colGray());
    }
  }
}

void drawSecondSweep(LGFX_Sprite& g, int seconds) {
  const float angle = (static_cast<float>(seconds) * 6.0f - 90.0f) * kDegToRad;
  const uint16_t c = colWhite();
  // Five parallel lines fanned by a small angular offset so the sweep
  // reads as a solid bar without needing anti-aliasing.
  static const float offs[] = {-0.020f, -0.010f, 0.0f, 0.010f, 0.020f};
  for (float d : offs) {
    drawRadialLine(g, angle + d, 88, 108, c);
  }
}

void drawTime(LGFX_Sprite& g, const std::tm& t) {
  // "1" is narrower in Font7 — nudge the anchor left when the hour starts
  // with '1' so the two glyphs land visually centered.
  const int cx = (t.tm_hour >= 10 && t.tm_hour <= 19) ? 115 : 120;
  const int cy = 108;
  char buf[8];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  g.setFont(&fonts::Font7);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(colWhite(), radar::kColorBackground);
  g.drawString(buf, cx, cy);
  g.setFont(&fonts::Font0);
  g.setTextDatum(textdatum_t::top_left);
}

void drawLabelValue(LGFX_Sprite& g, const char* label, const char* value,
                    int y, uint16_t label_color, uint16_t value_color) {
  const int cx = radar::kCenterX;
  g.setTextDatum(textdatum_t::top_center);
  g.setTextSize(1);
  char line[32];
  std::snprintf(line, sizeof(line), "%s  %s", label, value);
  g.setTextColor(value_color, radar::kColorBackground);
  g.drawString(line, cx, y);
  (void)label_color;
  g.setTextDatum(textdatum_t::top_left);
}

void drawSensorBlock(LGFX_Sprite& g) {
  const services::outdoor_temp::Reading oat = services::outdoor_temp::cached();
  const services::env_sensor::Reading env = services::env_sensor::read();

  char oat_val[16];
  if (oat.valid) std::snprintf(oat_val, sizeof(oat_val), "%.0fF",
                               std::round(oat.tempF));
  else           std::snprintf(oat_val, sizeof(oat_val), "--F");
  drawLabelValue(g, "OAT", oat_val, 148, colGray(), colTemp());

  if (env.valid) {
    char cabin_val[16];
    char rh_val[16];
    std::snprintf(cabin_val, sizeof(cabin_val), "%.0fF",
                  std::round(env.tempF));
    std::snprintf(rh_val, sizeof(rh_val), "%.0f%%",
                  std::round(env.humidityPct));
    drawLabelValue(g, "CABIN", cabin_val, 166, colGray(), colTemp());
    drawLabelValue(g, "RH",    rh_val,    184, colGray(), colTemp());
  }
}

// Filled triangle arrow rotated to `angle_rad` (0 rad = pointing right).
// Base at (cx, cy), length `len`, half-width `half_w`.
void drawArrow(LGFX_Sprite& g, int cx, int cy, float angle_rad, int len,
               int half_w, uint16_t color) {
  const float cs = std::cos(angle_rad);
  const float sn = std::sin(angle_rad);
  // Tip of the arrow.
  const float tx = cx + cs * len;
  const float ty = cy + sn * len;
  // Base corners: perpendicular to the arrow axis, at the base end.
  const float bx = cx - cs * (len * 0.15f);
  const float by = cy - sn * (len * 0.15f);
  const float px = -sn * half_w;
  const float py =  cs * half_w;
  g.fillTriangle(static_cast<int>(std::lroundf(tx)),
                 static_cast<int>(std::lroundf(ty)),
                 static_cast<int>(std::lroundf(bx + px)),
                 static_cast<int>(std::lroundf(by + py)),
                 static_cast<int>(std::lroundf(bx - px)),
                 static_cast<int>(std::lroundf(by - py)),
                 color);
}

// Garmin-style wind indicator: small arrow showing where the wind is
// BLOWING TO (i.e. 180° from the meteorological "wind from" direction),
// plus digital "270°/12kt" text. Placed above the time so it stays
// clear of the seven-segment glyphs.
void drawWindIndicator(LGFX_Sprite& g) {
  const services::outdoor_temp::Reading r = services::outdoor_temp::cached();
  const int block_cy = 52;
  const int arrow_cx = radar::kCenterX - 30;
  const uint16_t c = colGreen();

  g.setFont(&fonts::Font0);
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_left);
  g.setTextColor(c, radar::kColorBackground);

  if (!r.valid || std::isnan(r.windKts) || std::isnan(r.windDegFrom)) {
    g.drawString("WND --", radar::kCenterX - 18, block_cy);
    g.setTextDatum(textdatum_t::top_left);
    return;
  }

  // Meteorological "wind from" → direction wind is blowing TO is 180° off.
  // Screen angle convention: 0 rad = +x axis (right), and +y is DOWN, so a
  // heading of 0° (north) points UP → screen angle = -90°. Subtract 90°
  // from the compass heading to convert.
  const float going_to_deg = r.windDegFrom + 180.0f;
  const float angle_rad = (going_to_deg - 90.0f) * kDegToRad;
  drawArrow(g, arrow_cx, block_cy, angle_rad, 10, 4, c);

  char buf[16];
  std::snprintf(buf, sizeof(buf), "%03d/%.0fkt",
                static_cast<int>(std::lroundf(r.windDegFrom)) % 360,
                std::round(r.windKts));
  g.drawString(buf, arrow_cx + 14, block_cy);
  g.setTextDatum(textdatum_t::top_left);
}

// PFD-style altimeter setting (Kollsman window): green digital text in a
// thin box. Placed at the bottom center so it doesn't clash with the
// CABIN/RH lines when a BME280 is installed.
void drawBaroIndicator(LGFX_Sprite& g) {
  const services::outdoor_temp::Reading r = services::outdoor_temp::cached();
  const int block_cy = 205;
  const int half_w = 36;
  const int half_h = 8;
  const uint16_t c = colGreen();

  g.drawRect(radar::kCenterX - half_w, block_cy - half_h,
             half_w * 2, half_h * 2, colFrame());

  g.setFont(&fonts::Font0);
  g.setTextSize(1);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(c, radar::kColorBackground);
  char buf[16];
  if (r.valid && !std::isnan(r.baroInHg)) {
    std::snprintf(buf, sizeof(buf), "%.2f IN", r.baroInHg);
  } else {
    std::snprintf(buf, sizeof(buf), "--.-- IN");
  }
  g.drawString(buf, radar::kCenterX, block_cy);
  g.setTextDatum(textdatum_t::top_left);
}

bool localTimeNow(std::tm* out) {
  const std::time_t now = std::time(nullptr);
  // On ESP32, before SNTP has synced, time() returns a very small value
  // (seconds since boot cast forward). Treat anything before 2020-01-01
  // as "not synced yet" so the display shows the "SYNC" placeholder.
  if (now < 1577836800L) return false;
  std::tm* tm_ptr = std::localtime(&now);
  if (tm_ptr == nullptr) return false;
  *out = *tm_ptr;
  return true;
}

void drawUnsyncedPlaceholder(LGFX_Sprite& g) {
  g.setFont(&fonts::Font7);
  g.setTextDatum(textdatum_t::middle_center);
  g.setTextColor(colAmber(), radar::kColorBackground);
  g.drawString("--:--", 120, 108);
  g.setFont(&fonts::Font0);
  g.setTextDatum(textdatum_t::top_center);
  g.setTextSize(1);
  g.setTextColor(colAmber(), radar::kColorBackground);
  g.drawString("SYNC", radar::kCenterX, 148);
  g.setTextDatum(textdatum_t::top_left);
}

}  // namespace

void init() {
  services::outdoor_temp::init();
  services::env_sensor::init();
}

void refresh() {
  services::outdoor_temp::loop();
}

void draw() {
  LGFX_Sprite* sp = radarDisplayFrameSprite();
  if (sp == nullptr) return;  // no fallback — cockpit screen requires sprite.
  LGFX_Sprite& g = *sp;

  g.fillScreen(radar::kColorBackground);
  drawTicks(g);
  drawWindIndicator(g);

  std::tm t{};
  if (localTimeNow(&t)) {
    drawTime(g, t);
    drawSensorBlock(g);
    drawSecondSweep(g, t.tm_sec);
  } else {
    drawUnsyncedPlaceholder(g);
    drawSensorBlock(g);
  }

  drawBaroIndicator(g);

  // Round bezel mask — same as radar/weather screens so SDL matches
  // physical panel.
  g.fillArc(radar::kCenterX, radar::kCenterY, radar::kSize + 8, 120, 0, 360,
            radar::kColorBackground);
  g.pushSprite(0, 0);
}

}  // namespace ui::cockpit
