#pragma once

// Native-only: a small HTTP settings server that mirrors the hardware's
// WiFiManager captive portal. Same form field names (radar_lat,
// radar_lon, metar_lat/lon/rad, focus_ring, show_runways), so the
// muscle memory transfers directly from emulator to hardware.
//
// Listens on 127.0.0.1:8080 in a background thread. The main SDL loop
// applies any pending state changes on each frame — no cross-thread
// mutation of the render state.
//
// Persists edits to emulator_config.json at the repo root so restarts
// remember the last-saved config.

#ifdef USE_NATIVE

namespace host::config_server {

// Load emulator_config.json (if present) and apply its values to the
// live service state — services::location, services::metar_config,
// services::focus. Call once during boot after those services'
// own init() has run.
void loadPersistedConfig();

// Start the HTTP server on 127.0.0.1:8080. Idempotent; safe to call
// multiple times. Spawns a background thread.
void start(int port = 8080);

// Drain any pending state changes queued by the HTTP thread and apply
// them on the caller's thread (the main SDL loop). Cheap when no
// changes are pending. Returns true if any state was applied — the
// caller should redraw.
bool applyPending();

// Stop the server + join the background thread. Best-effort — safe to
// omit at process exit.
void stop();

}  // namespace host::config_server

#endif  // USE_NATIVE
