// HTTPS-driven tile fetch. Called periodically from the main loop —
// checks whether the saved location has moved to a new tile since the
// last fetch, and if so pulls the fresh tile from
// https://radar.benyaffe.com/data/tiles/{z}/{x}/{y}.bin, puts it in
// the RAM TileStore, and persists it to SPIFFS via services::tile_cache.
//
// Fetch cadence contract (from the refactor plan):
//   * Only when the location tile changes — not on boot, not on
//     panning within the same tile, not periodically.
//   * At most one fetch per location change; if it fails, retry on
//     the next loop until it succeeds or the location changes again.
//
// ESP32-only. The native emulator has no live network fetch path;
// its bootstrap tile comes from disk (src/host/host_stubs.cpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace services::tile_fetch {

// Called every main-loop tick. Cheap when the location hasn't moved
// to a new tile — just compares (z, x, y) against the last fetched
// key. Kicks off a fetch when they differ.
void loop();

// Pure helper — has the saved location moved to a different tile at
// zoom `z` since the last-recorded (x, y)? Returns true on first call
// (never fetched), and when the tile key changes. Exposed for testing.
bool shouldFetch(uint8_t z, double lat, double lon,
                  uint8_t last_z, uint16_t last_x, uint16_t last_y,
                  bool have_last);

}  // namespace services::tile_fetch
