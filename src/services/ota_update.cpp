#include "services/ota_update.h"

#ifdef USE_NATIVE

// Native/SDL build: no OTA. Empty stub keeps main.cpp/host_main.cpp
// callers uniform.
namespace services::ota {
void loop() {}
void setHostname(const char*) {}
}  // namespace services::ota

#else

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>

#include "config.h"

namespace services::ota {
namespace {

constexpr char kPrefsNamespace[] = "wifi";
constexpr char kPrefsHostnameKey[] = "ota_host";
constexpr uint16_t kPort = 3232;

bool s_initialized = false;

String loadHostname() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return String(config::kPortalHostname);
  }
  String host = prefs.getString(kPrefsHostnameKey, config::kPortalHostname);
  prefs.end();
  if (host.length() == 0) host = String(config::kPortalHostname);
  return host;
}

void ensureInitialized() {
  if (s_initialized) return;
  if (WiFi.status() != WL_CONNECTED) return;

  const String host = loadHostname();
  ArduinoOTA.setHostname(host.c_str());
  ArduinoOTA.setPort(kPort);
  // No password: LAN-only via mDNS. Fine for the target use case (personal
  // device on your own home network). Users who want auth can add it here.

  ArduinoOTA.onStart([]() {
    const char* type =
        (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
    Serial.printf("ota: %s update starting\n", type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("ota: update complete, rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    static unsigned int last_pct = 0;
    const unsigned int pct =
        (total == 0) ? 0 : static_cast<unsigned int>((done * 100U) / total);
    if (pct != last_pct && (pct % 10) == 0) {
      Serial.printf("ota: %u%%\n", pct);
      last_pct = pct;
    }
  });
  ArduinoOTA.onError([](ota_error_t err) {
    Serial.printf("ota: error %u\n", static_cast<unsigned>(err));
  });

  ArduinoOTA.begin();
  s_initialized = true;
  Serial.printf("ota: ready as '%s.local' on port %u\n", host.c_str(), kPort);
}

}  // namespace

void loop() {
  if (!s_initialized) {
    ensureInitialized();
    return;
  }
  ArduinoOTA.handle();
}

void setHostname(const char* hostname) {
  if (hostname == nullptr || hostname[0] == '\0') return;
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) return;
  prefs.putString(kPrefsHostnameKey, hostname);
  prefs.end();
  Serial.printf("ota: hostname saved (%s) — takes effect on next boot\n",
                hostname);
}

}  // namespace services::ota

#endif
