// Aircraft fetch + parse. Calls /api/adsb (proxied to
// airplanes.live / opendata.adsb.fi by the Netlify Function at
// web/netlify/functions/adsb.mjs) and normalizes the response into a
// shape aligned with the firmware's services::adsb::Aircraft struct.

// Receiver-source tiers for the ghost-dedup pass. Higher tier =
// more trustworthy. Any unrecognized adsb.fi "type" string becomes 0
// (Unknown) so a new source name added upstream can never crash us.
export type AdsbTier = 0 | 1 | 2 | 3;

export interface Aircraft {
  hex: string;
  callsign: string;    // trimmed "flight" > registration (never hex)
  reg: string;         // trimmed "r" — used only by the ghost-dedup identity guard
  type: string;        // "B738", "A320", etc; empty if unknown
  lat: number;
  lon: number;
  altFt: number | null;   // barometric altitude (null = on ground / unknown)
  gsKnots: number;        // ground speed
  trackDeg: number;       // direction of travel
  noseDeg: number;        // nose heading (falls back to track)
  vsFpm: number;          // vertical rate; 0 if unknown
  squawk: number;         // transponder code (0 = unknown)
  sourceTier: AdsbTier;   // adsb.fi "type" mapped to a confidence tier
}

interface AdsbRawAircraft {
  hex?: string;
  flight?: string;
  r?: string;
  t?: string;
  type?: string;          // adsb.fi receiver-source string (adsb_icao, mlat, …)
  lat?: number;
  lon?: number;
  alt_baro?: number | "ground";
  alt_geom?: number;
  gs?: number;
  tas?: number;
  ias?: number;
  track?: number;
  true_heading?: number;
  mag_heading?: number;
  baro_rate?: number;
  geom_rate?: number;
  squawk?: string;
}

function sourceTier(rawType: string | undefined): AdsbTier {
  switch (rawType) {
    case "adsb_icao":
    case "adsb_icao_nt":
      return 3;
    case "adsr_icao":
    case "tisb_icao":
      return 2;
    case "mlat":
      return 1;
    default:
      // tisb_trackfile / tisb_other / adsb_other / mode_s / adsc / absent /
      // anything upstream adds tomorrow → tier 0, droppable by a higher-
      // tier neighbor.
      return 0;
  }
}

interface AdsbResponse {
  ac?: AdsbRawAircraft[];        // opendata.adsb.fi field name
  aircraft?: AdsbRawAircraft[];  // fallback for other ADS-B sources
}

function pickCallsign(raw: AdsbRawAircraft): string {
  const flight = (raw.flight ?? "").trim();
  if (flight) return flight;
  const reg = (raw.r ?? "").trim();
  if (reg) return reg;
  // No fallback to hex: synthetic ~-prefixed TIS-B / MLAT / ADS-R ids
  // read as garbage on the display. Mirrors the firmware fillTagFields.
  return "";
}

function pickAltitude(raw: AdsbRawAircraft): number | null {
  if (raw.alt_baro === "ground") return null;
  if (typeof raw.alt_baro === "number") return raw.alt_baro;
  if (typeof raw.alt_geom === "number") return raw.alt_geom;
  return null;
}

function pickGroundSpeed(raw: AdsbRawAircraft): number {
  return raw.gs ?? raw.tas ?? raw.ias ?? 0;
}

function pickTrack(raw: AdsbRawAircraft): number {
  return raw.track ?? raw.true_heading ?? raw.mag_heading ?? 0;
}

function pickNose(raw: AdsbRawAircraft): number {
  return raw.true_heading ?? raw.mag_heading ?? raw.track ?? 0;
}

function pickVerticalRate(raw: AdsbRawAircraft): number {
  return raw.baro_rate ?? raw.geom_rate ?? 0;
}

function pickSquawk(raw: AdsbRawAircraft): number {
  const s = raw.squawk;
  if (!s) return 0;
  const n = parseInt(s, 10);
  return isFinite(n) ? n : 0;
}

function normalize(raw: AdsbRawAircraft): Aircraft | null {
  if (typeof raw.lat !== "number" || typeof raw.lon !== "number") return null;
  return {
    hex: raw.hex ?? "",
    callsign: pickCallsign(raw),
    reg: (raw.r ?? "").trim(),
    type: (raw.t ?? "").trim(),
    lat: raw.lat,
    lon: raw.lon,
    altFt: pickAltitude(raw),
    gsKnots: pickGroundSpeed(raw),
    trackDeg: pickTrack(raw),
    noseDeg: pickNose(raw),
    vsFpm: pickVerticalRate(raw),
    squawk: pickSquawk(raw),
    sourceTier: sourceTier(raw.type),
  };
}

// --- Ghost dedup ----------------------------------------------------------
// Same design as the firmware implementation in src/services/adsb_dedup.cpp
// (see that file's header comment for the rationale). Runs after every
// fetch, before the list is exposed. The identity guard is load-bearing:
// without it, aircraft in close formation on approach get spuriously
// merged.

const MAX_LATERAL_METERS = 460;    // ~0.25 nm
const MAX_ALT_DIFF_FT = 300;
const MAX_TRACK_DIFF_DEG = 30;
const MAX_SPEED_RATIO = 0.30;

function normalizeIdent(s: string): string {
  const stripped = s.startsWith("~") ? s.slice(1) : s;
  return stripped.trim().toUpperCase();
}

function identsMatch(a: string, b: string): boolean {
  const na = normalizeIdent(a);
  const nb = normalizeIdent(b);
  if (!na || !nb) return false;
  return na === nb;
}

function approxDistMeters(
  lat1: number, lon1: number, lat2: number, lon2: number,
): number {
  const kMetersPerDegLat = 111320;
  const midLatRad = ((lat1 + lat2) * 0.5) * Math.PI / 180;
  const mx = (lon2 - lon1) * kMetersPerDegLat * Math.cos(midLatRad);
  const my = (lat2 - lat1) * kMetersPerDegLat;
  return Math.sqrt(mx * mx + my * my);
}

function geometryLooksLikeGhost(a: Aircraft, b: Aircraft): boolean {
  if (approxDistMeters(a.lat, a.lon, b.lat, b.lon) > MAX_LATERAL_METERS) {
    return false;
  }
  if (a.altFt !== null && b.altFt !== null) {
    if (Math.abs(a.altFt - b.altFt) > MAX_ALT_DIFF_FT) return false;
  }
  if (a.trackDeg > 0 && b.trackDeg > 0) {
    let dt = Math.abs(a.trackDeg - b.trackDeg);
    if (dt > 180) dt = 360 - dt;
    if (dt > MAX_TRACK_DIFF_DEG) return false;
  }
  if (a.gsKnots > 0 && b.gsKnots > 0) {
    const bigger = Math.max(a.gsKnots, b.gsKnots);
    if (Math.abs(a.gsKnots - b.gsKnots) / bigger > MAX_SPEED_RATIO) {
      return false;
    }
  }
  return true;
}

function identityGuardPasses(low: Aircraft, high: Aircraft): boolean {
  const lowHasId = low.callsign !== "" || low.reg !== "";
  if (!lowHasId) return true;
  if (identsMatch(low.callsign, high.callsign)) return true;
  if (identsMatch(low.reg, high.reg)) return true;
  return false;
}

export function deduplicateGhosts(list: Aircraft[]): Aircraft[] {
  if (list.length < 2) return list;
  const drop = new Array<boolean>(list.length).fill(false);
  for (let i = 0; i < list.length; i++) {
    if (drop[i]) continue;
    for (let j = i + 1; j < list.length; j++) {
      if (drop[j]) continue;
      const ti = list[i].sourceTier;
      const tj = list[j].sourceTier;
      if (ti === tj) continue;
      if (!geometryLooksLikeGhost(list[i], list[j])) continue;
      const lowIdx = ti < tj ? i : j;
      const highIdx = ti < tj ? j : i;
      if (identityGuardPasses(list[lowIdx], list[highIdx])) {
        drop[lowIdx] = true;
        if (lowIdx === i) break;
      }
    }
  }
  return list.filter((_, i) => !drop[i]);
}

// Cache the last successful payload so a transient fetch failure doesn't
// blank the display.
let s_aircraft: Aircraft[] = [];
let s_lastUpdateMs = 0;
let s_lastError: string | null = null;
let s_fetchCount = 0;
// Monotonic request id — every fetchAircraft call captures the value at
// entry; when it resolves, it only writes s_aircraft if its id still
// matches. Also bumped by clearAircraft so any in-flight fetch for the
// old center discards its result instead of overwriting the fresh clear.
let s_gen = 0;

export function aircraft(): readonly Aircraft[] {
  return s_aircraft;
}

export function lastUpdateMs(): number {
  return s_lastUpdateMs;
}

export function fetchCount(): number {
  return s_fetchCount;
}

export function lastError(): string | null {
  return s_lastError;
}

// Drop the cached aircraft list. Callers invoke this the moment the
// active center changes so old-center aircraft don't linger and get
// projected off the visible disc.
export function clearAircraft(): void {
  s_aircraft = [];
  s_lastError = null;
  s_gen++;
}

export async function fetchAircraft(
  centerLat: number,
  centerLon: number,
  nm: number,
): Promise<void> {
  const myGen = ++s_gen;
  // cache: 'no-store' so the browser doesn't reuse a stale copy after
  // the Worker's short edge-cache window. Aircraft are moving; the API
  // is our single source of freshness.
  const url =
    `api/adsb?lat=${centerLat.toFixed(4)}` +
    `&lon=${centerLon.toFixed(4)}&nm=${nm.toFixed(1)}`;
  // Abort after 5 s so a stalled upstream doesn't freeze the poll loop.
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 5000);
  try {
    const resp = await fetch(url, {
      cache: "no-store",
      signal: controller.signal,
    });
    if (myGen !== s_gen) return;
    if (!resp.ok) {
      s_lastError = `HTTP ${resp.status}`;
      return;
    }
    const doc = (await resp.json()) as AdsbResponse;
    if (myGen !== s_gen) return;
    const list: Aircraft[] = [];
    for (const raw of doc.ac ?? doc.aircraft ?? []) {
      const a = normalize(raw);
      if (a) list.push(a);
    }
    s_aircraft = deduplicateGhosts(list);
    s_lastUpdateMs = Date.now();
    s_lastError = null;
    s_fetchCount += 1;
  } catch (err) {
    if (myGen !== s_gen) return;
    s_lastError = err instanceof Error ? err.message : String(err);
  } finally {
    clearTimeout(timer);
  }
}
