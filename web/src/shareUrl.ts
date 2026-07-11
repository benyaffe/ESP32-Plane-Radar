// Shareable-URL layer — encodes the current view (radar center + range,
// cockpit home, weather METAR center + radius) as URL query params so
// links can point someone at a specific view. Read once at boot, then
// written continuously via history.replaceState as the user interacts.
//
// Non-destructive: the URL never touches localStorage. Session-only
// overrides live in-memory only (see setSessionHome / setSessionMetar
// in state.ts). Opening Settings + Save promotes an override to the
// persisted cookie.

import { state, setCenter, setSessionHome, setSessionMetar, setView } from "./state";
import type { AirportIndexRow } from "./data";
import { search as airportSearch } from "./airports";
import { RANGE_PRESETS } from "./theme";

// Format used everywhere: 4 decimal places on lat/lon (11-meter
// precision) so URLs stay short-ish while remaining locally accurate.
function formatCoord(n: number): string {
  return n.toFixed(4);
}

// Parse "KSFO" or "37.6188,-122.375" into a {lat, lon, label}. The label
// is the ICAO when resolved via the airport index, otherwise a plain
// "lat,lon" string. Returns null on failure so callers can fall back
// silently.
function resolvePoint(
  raw: string,
  airportIndex: AirportIndexRow[],
): { lat: number; lon: number; label: string } | null {
  const s = raw.trim();
  if (!s) return null;

  if (s.includes(",")) {
    const [latStr, lonStr] = s.split(",", 2);
    const lat = parseFloat(latStr);
    const lon = parseFloat(lonStr);
    if (!isFinite(lat) || !isFinite(lon)) return null;
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return null;
    return { lat, lon, label: `${formatCoord(lat)},${formatCoord(lon)}` };
  }

  // Non-comma → treat as an airport code / typeahead query. The search
  // ranker exact-matches ICAO/IATA at the top so "KSFO"/"SFO" both hit.
  const hits = airportSearch(airportIndex, s, 1);
  if (hits.length === 0) return null;
  const [icao, , , , lat, lon] = hits[0];
  return { lat, lon, label: icao };
}

/** Apply URL params from `location.search` to app state. Called once
 *  at boot after the airport index has loaded. Missing params are
 *  silently ignored — no error surfaces to the user. */
export function applyUrlParams(airportIndex: AirportIndexRow[]): void {
  const qs = new URLSearchParams(window.location.search);
  const view = qs.get("view");

  const home = qs.get("home");
  if (home) {
    const p = resolvePoint(home, airportIndex);
    if (p) setSessionHome({ lat: p.lat, lon: p.lon });
  }

  const metar = qs.get("metar");
  const rad = qs.get("rad");
  if (metar) {
    const p = resolvePoint(metar, airportIndex);
    const radNm = rad ? parseFloat(rad) : state.metar.radiusNm;
    if (p && isFinite(radNm) && radNm > 0) {
      setSessionMetar({ centerLat: p.lat, centerLon: p.lon, radiusNm: radNm });
    }
  }

  const center = qs.get("center");
  if (center) {
    const p = resolvePoint(center, airportIndex);
    if (p) setCenter(p.lat, p.lon, p.label);
  }
  const range = qs.get("range");
  if (range) {
    const rangeNm = parseFloat(range);
    const idx = RANGE_PRESETS.findIndex((r) => Math.abs(r.nm - rangeNm) < 0.5);
    if (idx >= 0) state.rangeIdx = idx;
  }

  if (view === "weather" || view === "cockpit" || view === "radar") {
    setView(view);
  }
}

/** Rewrite `location.search` from current state. Uses replaceState so
 *  Back button doesn't fill up with intermediate URLs. Called from a
 *  state subscriber in main.ts on every notify(). */
export function updateShareUrl(): void {
  const qs = new URLSearchParams();
  qs.set("view", state.view);
  if (state.view === "radar") {
    // Prefer an ICAO center label when the ring's focus point has one
    // (labels come from the picker, which writes ICAO into the row).
    const label = state.centerLabel;
    const isIcaoLike = /^[A-Z]{3,4}$/.test(label);
    const centerStr = isIcaoLike
      ? label
      : `${formatCoord(state.centerLat)},${formatCoord(state.centerLon)}`;
    qs.set("center", centerStr);
    qs.set("range", String(RANGE_PRESETS[state.rangeIdx].nm));
  } else if (state.view === "cockpit") {
    qs.set("home", `${formatCoord(state.home.lat)},${formatCoord(state.home.lon)}`);
  } else if (state.view === "weather") {
    qs.set("metar", `${formatCoord(state.metar.centerLat)},${formatCoord(state.metar.centerLon)}`);
    qs.set("rad", String(state.metar.radiusNm));
  }
  const next = `${window.location.pathname}?${qs.toString()}${window.location.hash}`;
  // Only replaceState if the URL actually changed — avoids piling up
  // duplicate entries in the browser's replaceState log during rapid
  // ticks (1 Hz cockpit repaint could otherwise fire it every second).
  if (next !== window.location.pathname + window.location.search + window.location.hash) {
    window.history.replaceState(null, "", next);
  }
}
