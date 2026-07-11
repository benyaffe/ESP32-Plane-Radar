# Fork changelog

Personal fork of [`MatixYo/ESP32-Plane-Radar`](https://github.com/MatixYo/ESP32-Plane-Radar) — same firmware core, with a lot bolted on top so the desk toy runs on real hardware, in a Mac window, and in a browser.

The upstream firmware and 3D-printed case are unchanged; everything here is additive. Files touched are grouped by feature so each section can be reviewed independently.

---

## 1. Desktop emulator (`pio run -e native`)

The full radar renders in a Mac window backed by LovyanGFX's SDL panel. Iteration on any UI change is now sub-second — no flash cycle, no hardware.

- **New PlatformIO env** `[env:native]` in [`platformio.ini`](platformio.ini). Firmware env `[env:supermini]` is untouched.
- **Host shim tree** in [`host_shims/`](host_shims/) — Arduino/HTTPClient/WiFi/Preferences/`nvs_flash` stubs so firmware modules link cleanly against a desktop libc.
- **`src/host/host_main.cpp`** — desktop `setup()`/`loop()` that skips the WiFi setup portal and drives the panel from `emscripten_set_main_loop`-style poll.
- **`src/host/host_stubs.cpp`** — native implementations of `services::adsb::fetchUpdate` (via `popen(curl)`) and `services::location` with a compile-time center.
- **Screenshot loop** — every 200 ms the framebuffer is dumped to `/tmp/plane-radar-screenshot.ppm` for closed-loop visual testing (see [`scripts/dev/snap.sh`](scripts/dev/snap.sh)).
- **Bezel mask** — the SDL panel is square 240×240, but the physical GC9A01 is a disc of radius ~120; a final pass paints the corner region back to background so what you see on the Mac matches what you'd see through the bezel.

## 2. Global tile pipeline

All map data — coastlines, land polygons, per-airport runways — flows through one binary tile format shared by the firmware, the emulator, and the website. No more per-region bakes.

- **`scripts/build_tiles.py`** — downloads Natural Earth 10 m coastline/land/minor_islands/lakes + OurAirports airports.csv/runways.csv, then emits `web/public/data/tiles/{z}/{x}/{y}.bin` at three zoom levels (z=3 continental, z=5 regional, z=7 local at 0.002° / ~222 m — the plan's coastline-quality baseline).
- **`scripts/tile_{format,scheme,coastline,polygons,airports,geo}.py`** — one file per concern: byte layout, tile grid math, per-layer builders, shared geometry helpers.
- **`.github/workflows/build-tiles.yml`** — monthly cron rebakes the pyramid from live sources and commits `web/public/data/tiles/` back to the branch; `deploy-web.yml` then ships it to Netlify. Includes a smoke check that fails the workflow loudly if the pyramid comes out under 3000 tiles or missing a zoom level or missing the SF-area canary tile — a silent bad bake would ship stale/broken tiles for a whole month before anyone noticed. Writes `web/public/data/tiles/BAKE_TIMESTAMP.txt` on every run so a future freshness monitor has something machine-readable to compare against. **Shipping gotcha:** GitHub Actions scheduled workflows only fire from the default branch, so this cron doesn't take over from the initial hand-committed bootstrap until this branch merges to `main`.
- **`data/emulator_bootstrap_tile_7_20_37.bin`** — the SF-area z=7 tile checked into the repo so the emulator boots with a real map immediately (no fetch on startup — see section 3).
- **`data/instrument_approach_airports.txt`** — hand-curated seed list of ~65 US airports the FAA marks as instrument-approach-capable. Merged into every tile's airports set alongside the OurAirports data.

## 3. Firmware tile fetch + cache

The device pulls the one z=7 tile covering its current location over HTTPS on first boot (or after the location changes) and keeps it in flash forever.

- **`include/data/tile_{store,reader,math}.h`** + implementations — in-RAM LRU cache backed by a SPIFFS partition. `TileStore.get()` returns either the cached tile or the compiled-in **fallback tile** (`src/data/fallback_tile.cpp`, ~14 KB, world coastlines/land at coarse detail) so boot is never blank.
- **`src/services/tile_fetch.cpp`** — HTTPS fetch from `radar.benyaffe.com/data/tiles/{z}/{x}/{y}.bin`. Triggered only when the *saved location* moves to a different tile — never on boot, never on panning, never periodically.
- **`src/geo/ear_clip.cpp`** — on-device polygon triangulator so land polygons fetched at runtime can render on the SPI panel (which only knows how to fill triangles).
- **`src/ui/{coastline,land,runway}_overlay.cpp`** — each overlay reads its layer from the current tile and draws through the shared `latLonToScreen` / `clipSegmentToDisc` projection primitives in `src/ui/map_projection.cpp`.
- **`src/ui/weather_map.cpp`** — the METAR view enumerates the (up to 4) z=7 tiles overlapping its user-configurable viewport and draws land + coast from each, so the map still renders correctly when the METAR center sits near a tile boundary.
- **Offline banner** — if Wi-Fi drops mid-session, after a 4 s grace period the screen replaces the stale radar frame with a full "No Wi-Fi / Retrying / Hold BOOT 3 sec to reset network" banner. Clears back to the radar when Wi-Fi returns.

## 4. Aircraft data blocks (tags)

Callsign + altitude labels next to each icon, with the same style choices ATC uses on approach scopes.

- **`callsign` picking** — trims the ADS-B `flight` field first; if empty, falls back to registration (`r`), then hex.
- **2-line tag** — line 1 = callsign; line 2 alternates altitude (in hundreds of feet, aviation convention) and type code (e.g. `A320`). Toggle timed halfway between position updates so the flip is deliberate, not fighting the icon jump.
- **Trend triangle** — up/down arrow on the altitude when |vertical rate| ≥ 500 fpm.
- **Emergency squawk (7500/7600/7700)** — icon + tag render red, and an `EM` glyph pins to the tag's free corner opposite the alt/type block.
- **Two-tier collision** — labels register as HARD rectangles; icons + track vectors as SOFT. Aircraft tags first try to avoid all rects (strict), then fall back to avoiding just labels (relaxed). Result: labels never cover other labels but *may* cover a deprioritized aircraft's icon — which is what you want.
- **Tag budget per range** — `{5nm: 20, 10nm: 15, 15nm: 10, 25nm: 6}`. Aircraft ranked by clarity = `alt_ft + gs*20 + |vs|/5` (+ big bonus for emergency); top N get labels, everyone else keeps their icon but drops the callsign. Wide views stay legible.
- **On-ground filter** — ADS-B records reporting "ground" or (< 100 ft AND < 40 kt) are dropped entirely (no icon, no tag). KSFO alone has ~30 ground emitters; filtering keeps the view about flying traffic.

## 5. Nautical miles + range presets

Aviation convention throughout.

- **Range preset ring** — 5 / 10 / 15 / 25 nm (was 5 / 10 / 15 / 25 km).
- **Range label** — sits inside the outer ring on the E-of-N spoke by default, walks around symmetrically if it would collide with an airport label.
- **N/E/S/W cardinals removed** — north is always up on a radar; the cardinals were spending pixels on redundant info and crowding the bezel.

## 6. Two-gesture screen ring + case-tap input

The whole app is one ring of 5 screens navigated with two gestures — no mode context to remember.

- **Screen ring:** `Radar @ Home → Radar @ Focus 2 → Radar @ Focus 3 → Weather → Cockpit → wraps`. Each focus airport is its own radar position; the focus and screen concepts are unified. Default focus ring is `{Home (Sutro), SFO, OAK}` — user-editable via Settings.
- **Gestures:** `single tap` = adjust the current screen (cycle range on radar, refresh METAR on weather, no-op on cockpit); `double tap` = advance to the next screen. Long-press BOOT 3 s still resets Wi-Fi.
- **`BootTap`** state machine — trimmed to `{None, Single, Double}`. Software window shrunk from 400 ms → 250 ms since there's no third-tap to wait for.
- **Optional ADXL345 accelerometer** (`src/services/tap_sensor.cpp`) — sits on the shared I²C bus at address 0x53. When present, its hardware SINGLE_TAP + DOUBLE_TAP interrupts short-circuit the software discriminator in `bootButtonConsumeEvent()`, so knocking the case fires gestures instantly. Silent-fail on missing hardware — the BOOT-button path remains as fallback. Aimed at retro enclosures where the BOOT button isn't reachable from outside.
- **`src/services/radar_location.h`** — `setOverride()` / `clearOverride()` so `lat()`/`lon()` transparently redirect while a focus point is active. No code outside `services::location` needs to know.
- **Persistence** — current focus index saved to Preferences.

## 7. Live weather view

A second view mode showing nearby airports as VFR/MVFR/IFR/LIFR colored dots.

- **`src/services/weather.cpp`** — bulk METAR fetch from `aviationweather.gov` (public, no key, works globally), local flight-category derivation (worst of ceiling and visibility per FAA rules).
- **`src/ui/weather_map.cpp`** — auto-fits the station bbox to the viewport, nudges overlapping dots apart, draws land + coastline underneath (from tiles, see section 3) for context, single-blit via the shared frame sprite so the freshness updater doesn't strobe.
- Reached via a double-tap from any of the three radar positions in the screen ring (see section 6).

## 8. Layer toggles

Every overlay individually on/off, persisted to Preferences.

- **`include/ui/layer_style.h`** + **`src/ui/layer_style.cpp`** — small registry: `enum class Layer { Coastline, Land, RunwaysLarge, RunwaysFocus, AircraftTags }`.
- **Keyboard bindings on native** — keys `1`–`5` flip the corresponding layer, `S` snaps a screenshot.

## 9. Web preview (`web/`)

Browser port so friends can try the interface without hardware. Same visual language, same interactions.

- **Vite + TypeScript** — no framework; ~14 KB of gzipped JS renders the whole thing.
- **Tile-based data** — the browser fetches the same `data/tiles/{z}/{x}/{y}.bin` files the firmware pulls, decodes them with **`web/src/tile.ts`** (byte-for-byte counterpart of `scripts/tile_format.py`), and iterates polylines / airports directly. Fetches are memoized per (z, x, y) in **`web/src/tileFetch.ts`** so panning across an already-loaded region is instant.
- **`scripts/build_web_data.py`** — bakes only `airport_index.json`, the global typeahead index used by the Settings picker. Everything else the browser needs comes from the tile pyramid.
- **`web/netlify/functions/adsb.mjs`** — Netlify Function that proxies `airplanes.live` / `opendata.adsb.fi` (no CORS on either). Also `metar.mjs` for the same reason on `aviationweather.gov`.
- **Weather** — talks to `aviationweather.gov` through the METAR proxy. Same upstream the firmware uses, works globally.
- **Touch + click + keyboard** — one central `Tap` discriminator handles all three input modes; single = adjust current screen, double = advance the same 5-screen ring as the hardware. Layer toggles are clickable buttons on mobile, `1`–`5` keys on desktop.
- **Deploy** — GitHub Actions → Netlify on every push to the deploying branch. See [`web/DEPLOY.md`](web/DEPLOY.md) for setup.

## 10. Test infrastructure

Three suites, one per surface — plus CI runs them all on every push.

- **Python (pytest)** for the tile pipeline: builders, tile scheme math, coastline quality contract.
- **C++ native (PlatformIO Unity)** for the firmware logic that runs off-device: tile parser, tile store LRU, coordinate/projection math, weather category classifier.
- **JavaScript (Vitest)** for the website: tile decoder, viewport-to-tile math, fetch client cache, projection.
- **`.github/workflows/tests.yml`** runs all three on every push; deploy only fires if tests pass.

## 11. Miscellaneous polish

- Land / water colors chosen so water reads as a subtle blue tint and land as near-black — matches how people describe what they expect to see.
- Bezel-radius label placement so nothing spills past the physical panel edge.
- Sprite double-buffering on both firmware and native — the weather view was strobing at 1 Hz before the sprite pass landed.

---

**Default center:** the public code centers on **Sutro Tower** (37.7552 N, 122.4528 W) — the SF broadcast landmark — not any personal address. The real firmware still asks you for your own location via the WiFi setup portal on first boot.

---

## Three-environment parity rule

The stack has **three visual environments** that must stay lockstep:

1. **Hardware firmware** (ESP32-C3 + GC9A01) — `pio run -e supermini`
2. **SDL desktop emulator** — `pio run -e native`, same C++ as firmware
3. **Web preview** — `web/`, TypeScript port

Firmware and SDL share source (only host stubs differ under `USE_NATIVE`); the web is a separate TypeScript codebase and cannot autoshare. To keep them from drifting:

- **The SDL emulator is source of truth for what "looks right."** It's the daily-driver during development and its pixels are what the firmware actually shows on a real panel (with the same BGR-swap chain applied).
- **Colors** — sample from an actual emulator PPM screenshot, not from the C++ constants. `include/ui/radar_theme.h` names colors by their *logical* aviation meaning (e.g. `kAircraftR=255` = "red"), but the panel's BGR order flips channels at display time. What ships on screen is `color565(kAircraftB, kAircraftG, kAircraftR)`, not the raw RGB. Web must mirror the *rendered* pixels, not the logical constants.
- **Placement math + timing** — every animation offset (e.g. tag alt/type flip lagging fetches by 1.5 s), every slot ring (16-slot tag placement), every clip-to-disc gate — implement in the emulator first, port to web second. Never the other way around.
- **Data** — geometric map data (coastlines, land, airports) comes from one binary tile pyramid at `web/public/data/tiles/{z}/{x}/{y}.bin`, produced by `scripts/build_tiles.py`. Firmware fetches over HTTPS and parses via `data::tile::TileReader`; web fetches over `fetch()` and parses via `web/src/tile.ts`. Both parsers must decode the same byte layout — `web/src/tile.test.ts` decodes a real committed tile as a guard against drift.
- **Feature adds** — new radar behaviour lands in `src/ui/*.cpp` first, gets pixel-sampled from the SDL emulator, then ported to `web/src/*.ts`. Any web-only additions (weather map, airport typeahead) don't need a firmware counterpart.

If a divergence bug lands (web looks different from emulator), the fix is *always* to make the web match the emulator, never the reverse.
