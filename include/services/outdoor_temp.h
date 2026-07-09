#pragma once

// Cached outdoor temperature from Open-Meteo (no key required). Fetched
// against the user's configured home location — see services::location —
// on a 15 min cadence. Non-blocking after the first successful fetch:
// the cockpit screen always reads whatever's cached and the fetch itself
// is driven by a call to loop() from the main loop.

namespace services::outdoor_temp {

struct Reading {
  float tempF;         // degrees Fahrenheit
  bool  valid;         // true after any successful fetch
  unsigned long age_ms;  // millis since last successful fetch
};

/** Cheap idempotent — safe to call at boot before WiFi. */
void init();

/** Kick a background fetch if the cache is stale. Blocking HTTP GET on the
 *  caller's thread; caps at ~7 s via HTTPClient timeout. Safe to call every
 *  loop iteration — will no-op until the interval elapses. */
void loop();

/** Latest cached reading. `valid` is false until the first successful fetch. */
Reading cached();

}  // namespace services::outdoor_temp
