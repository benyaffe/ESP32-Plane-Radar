#pragma once

// ArduinoOTA (espota) wrapper. Discovery via mDNS on port 3232; no password
// by default so any host on the LAN can flash. Hostname comes from the
// WiFi setup portal (Preferences namespace "wifi", key "ota_host"), or
// config::kPortalHostname if unset.

namespace services::ota {

/** Idempotent per-boot. Must be called after WiFi is up (mDNS setup). */
void loop();

/** Override the mDNS hostname (called from the portal-save callback). Takes
 *  effect on the next boot. Passing nullptr or empty string is ignored. */
void setHostname(const char* hostname);

}  // namespace services::ota
