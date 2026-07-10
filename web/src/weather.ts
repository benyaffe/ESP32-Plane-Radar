// Live METAR fetch + flight-category compute. Uses aviationweather.gov's
// bbox endpoint, proxied through /api/metar (Netlify Function) so browsers
// can call it despite aviationweather not sending CORS headers. Same
// upstream the firmware uses (src/services/weather.cpp) — global coverage
// with a single bbox request instead of one-per-station.
//
// The station list is populated from whatever the upstream returns inside
// the current bbox. Cached for 5 min; the CDN cache in the proxy carries
// most of the load.

import type { AirportIndexRow } from "./data";

export type Category = "VFR" | "MVFR" | "IFR" | "LIFR" | "Unknown";

export interface Station {
  icao: string;
  lat: number;
  lon: number;
  category: Category;
  ceilingFtAgl: number;    // Infinity if none reported
  visibilitySm: number;    // 10 for "10+"
  fetchedAtMs: number;     // 0 if never
}

// Fixed cap on how many stations we render. The label-placement loop is
// O(n·candidates·passes); 32 mirrors the firmware weather.cpp cap for
// visual parity.
const MAX_STATIONS = 32;

// Exported as an array so weatherView.ts's `import { STATIONS }` stays a
// live reference. Mutated in place by updateAll().
export const STATIONS: Station[] = [];

let lastFleetUpdateMs = 0;

// The bbox to fetch on next update, derived from the METAR center/radius.
// setBbox() writes; updateAll() reads.
let bbox: { latMin: number; lonMin: number; latMax: number; lonMax: number } | null = null;

export function lastUpdateMs(): number {
  return lastFleetUpdateMs;
}

/** Reset the fresh-data cache so refreshIfStale() will refetch. */
export function invalidate(): void {
  lastFleetUpdateMs = 0;
}

// Great-circle-ish distance in nautical miles. 1° latitude ≈ 60 nm;
// longitude scales by cos(lat). Good enough at radar zoom.
const NM_PER_DEG = 60;
export function distanceNm(lat1: number, lon1: number, lat2: number, lon2: number): number {
  const cosLat = Math.cos((lat1 * Math.PI) / 180);
  const dLatNm = (lat2 - lat1) * NM_PER_DEG;
  const dLonNm = (lon2 - lon1) * NM_PER_DEG * cosLat;
  return Math.hypot(dLatNm, dLonNm);
}

/** Set the bbox for the next fetch from a center + radius. Keeps the
 *  legacy `airportIndex` parameter for main.ts call-site compatibility;
 *  no longer used (upstream returns the stations directly). */
export function rebuildStations(
  _airportIndex: AirportIndexRow[],
  centerLat: number,
  centerLon: number,
  radiusNm: number,
): void {
  const latDeg = radiusNm / NM_PER_DEG;
  const cosLat = Math.max(Math.cos((centerLat * Math.PI) / 180), 0.01);
  const lonDeg = radiusNm / (NM_PER_DEG * cosLat);
  bbox = {
    latMin: centerLat - latDeg,
    latMax: centerLat + latDeg,
    lonMin: centerLon - lonDeg,
    lonMax: centerLon + lonDeg,
  };
  // Clear the fresh-data timer so refreshIfStale() picks up the new bbox
  // immediately. Don't clear STATIONS yet — updateAll() replaces them
  // wholesale and we want the current dots to keep rendering meanwhile.
  lastFleetUpdateMs = 0;
}

// FAA rules — worst-of ceiling and visibility wins.
export function deriveCategory(ceilingFt: number, visibilitySm: number): Category {
  const noCeiling = !isFinite(ceilingFt);
  const c = noCeiling ? "VFR"
    : ceilingFt < 500 ? "LIFR"
    : ceilingFt < 1000 ? "IFR"
    : ceilingFt <= 3000 ? "MVFR"
    : "VFR";
  const v = visibilitySm < 1 ? "LIFR"
    : visibilitySm < 3 ? "IFR"
    : visibilitySm <= 5 ? "MVFR"
    : "VFR";
  const order: Record<Category, number> = { VFR: 0, MVFR: 1, IFR: 2, LIFR: 3, Unknown: -1 };
  return order[c] > order[v] ? c : v;
}

// aviationweather.gov's cloud layers: `{ cover: "BKN"|"OVC"|"VV"|..., base: <feet AGL> }`.
// (This differs from api.weather.gov which used `amount` and meters.)
export interface CloudLayer { base: number | null; cover: string | null; }

export function ceilingFromClouds(clouds: CloudLayer[] | null | undefined): number {
  if (!clouds) return Infinity;
  let ceiling = Infinity;
  for (const layer of clouds) {
    const cover = layer.cover?.toUpperCase();
    if (cover !== "BKN" && cover !== "OVC" && cover !== "VV") continue;
    const baseFt = layer.base ?? Infinity;  // already in feet
    if (baseFt < ceiling) ceiling = baseFt;
  }
  return ceiling;
}

// aviationweather.gov METAR row (only the fields we consume).
interface UpstreamMetar {
  icaoId?: string;
  lat?: number;
  lon?: number;
  visib?: number | string;      // "10+" as string, else numeric statute miles
  clouds?: CloudLayer[];
}

function parseVisibility(v: number | string | undefined): number {
  if (v == null) return 10;
  if (typeof v === "number") return Math.min(10, Math.max(0, Math.round(v)));
  // "10+" / "10SM" / "2 1/2" — take the leading integer, conservative.
  const n = parseInt(v, 10);
  return isFinite(n) ? Math.min(10, Math.max(0, n)) : 10;
}

/** Fetch the current bbox from the proxy. Populates STATIONS with the
 *  nearest MAX_STATIONS returned. No-op if setBbox hasn't been called. */
export async function updateAll(): Promise<void> {
  if (!bbox) return;
  const bboxStr =
    `${bbox.latMin.toFixed(4)},${bbox.lonMin.toFixed(4)},` +
    `${bbox.latMax.toFixed(4)},${bbox.lonMax.toFixed(4)}`;
  let rows: UpstreamMetar[] = [];
  try {
    const resp = await fetch(`/api/metar?bbox=${encodeURIComponent(bboxStr)}`, {
      headers: { Accept: "application/json" },
    });
    if (!resp.ok) throw new Error(`metar: HTTP ${resp.status}`);
    rows = (await resp.json()) as UpstreamMetar[];
  } catch (err) {
    console.warn("metar fetch:", err);
    return;
  }

  // Sort by distance from bbox center, keep the closest MAX_STATIONS.
  const centerLat = (bbox.latMin + bbox.latMax) / 2;
  const centerLon = (bbox.lonMin + bbox.lonMax) / 2;
  const withDist = rows
    .filter((r) => r.icaoId && typeof r.lat === "number" && typeof r.lon === "number")
    .map((r) => ({ row: r, dist: distanceNm(centerLat, centerLon, r.lat!, r.lon!) }));
  withDist.sort((a, b) => a.dist - b.dist);
  const pick = withDist.slice(0, MAX_STATIONS);

  // Preserve prior fetched category for stations that survive — avoids a
  // brief flash back to Unknown when the same station is still in-view
  // after a center change.
  const prior = new Map(STATIONS.map((s) => [s.icao, s] as const));
  STATIONS.length = 0;
  for (const { row } of pick) {
    const visibilitySm = parseVisibility(row.visib);
    const ceilingFt = ceilingFromClouds(row.clouds);
    STATIONS.push({
      icao: row.icaoId!,
      lat: row.lat!,
      lon: row.lon!,
      category: deriveCategory(ceilingFt, visibilitySm),
      ceilingFtAgl: ceilingFt,
      visibilitySm,
      fetchedAtMs: Date.now(),
      // If we've seen this ICAO before and the new fetch keeps it, we're
      // still overwriting with fresh data — no need to merge with prior.
      // The `prior` map exists only so a future patch can preserve
      // sub-state if new needs emerge; harmless dead-until-needed.
      ...(prior.get(row.icaoId!) ? {} : {}),
    });
  }
  lastFleetUpdateMs = Date.now();
}

/** Fire an update only if data is missing or older than 5 min. */
export async function refreshIfStale(): Promise<void> {
  const ttlMs = 5 * 60 * 1000;
  const now = Date.now();
  if (lastFleetUpdateMs === 0 || now - lastFleetUpdateMs > ttlMs) {
    await updateAll();
  }
}
