/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/focus_points.h"
#include "services/metar_config.h"
#include "services/radar_location.h"
#include "services/wifi_setup.h"
#include "ui/layer_style.h"
#include "ui/radar_display.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"
#include "ui/weather_map.h"

namespace {

bool g_radar_visible = false;
bool g_weather_mode = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
unsigned long g_last_weather_draw_ms = 0;

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRangeLabel(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void onFocusTap() {
  services::focus::cycle();
  const auto& fp = services::focus::current();
  Serial.printf("Focus: %s\n", fp.name);
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    // Force a fresh fetch at the new center so the display updates fast.
    services::adsb::fetchUpdate(services::location::lat(),
                                services::location::lon(),
                                ui::radar::fetchRadiusKm());
    ui::radarDisplayDraw();
  }
}

void enterWeatherMode() {
  g_weather_mode = true;
  Serial.println("View: weather");
  if (WiFi.status() == WL_CONNECTED) ui::weather::refresh();
  ui::weather::draw();
  g_last_weather_draw_ms = millis();
}

void exitWeatherMode() {
  g_weather_mode = false;
  Serial.println("View: radar");
  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  const BootTap ev = bootButtonConsumeEvent();
  // In weather mode, ANY tap returns to radar. Triple stays as its own
  // gesture for re-entering weather from anywhere.
  if (g_weather_mode) {
    if (ev == BootTap::Triple) {
      // Force a refresh + redraw while staying in mode.
      enterWeatherMode();
    } else if (ev != BootTap::None) {
      exitWeatherMode();
    }
    return;
  }
  switch (ev) {
    case BootTap::Single: onRangeTap(); break;
    case BootTap::Double: onFocusTap(); break;
    case BootTap::Triple: enterWeatherMode(); break;
    case BootTap::None: break;
  }
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Plane Radar");

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal();
  }
  services::location::init();
  services::metar_config::init();
  ui::radar::rangeInit();
  services::focus::init();
  ui::layers::init();
  services::adsb::setPollFn(wifiLoop);

  if (wifiSetupConnect()) {
    showRadarIfConnected();
  }
}

void loop() {
  handleBootButton();
  wifiLoop();

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (g_weather_mode) {
      // Repaint the weather view every ~1s so the "n min ago" age updates
      // smoothly; refresh() itself no-ops until the 5 min TTL expires.
      if (millis() - g_last_weather_draw_ms >= 1000) {
        g_last_weather_draw_ms = millis();
        ui::weather::refresh();
        ui::weather::draw();
      }
    } else if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
    }
  }

  delay(10);
}
