// Live METAR fetch + flight-category compute. Uses the National Weather
// Service's api.weather.gov, which sends CORS `Access-Control-Allow-
// Origin: *` — so we call it directly from the browser, no proxy.
//
// One request per station (there's no bulk endpoint on NWS like there
// is on aviationweather.gov), fired in parallel. Cached for 5 min.

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

// Bay Area primary field set for the weather-dot view. Matches
// src/services/weather.cpp (minus KAPC/KWVI which were dropped there).
export const STATIONS: Station[] = [
  { icao: "KSFO", lat: 37.6188, lon: -122.3750, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KOAK", lat: 37.7213, lon: -122.2214, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KSJC", lat: 37.3639, lon: -121.9289, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KHWD", lat: 37.6591, lon: -122.1214, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KLVK", lat: 37.6934, lon: -121.8203, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KCCR", lat: 37.9897, lon: -122.0567, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KHAF", lat: 37.5136, lon: -122.5006, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KSQL", lat: 37.5119, lon: -122.2495, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KPAO", lat: 37.4611, lon: -122.1150, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KRHV", lat: 37.3329, lon: -121.8195, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
  { icao: "KNUQ", lat: 37.4161, lon: -122.0492, category: "Unknown", ceilingFtAgl: Infinity, visibilitySm: 0, fetchedAtMs: 0 },
];

let lastFleetUpdateMs = 0;

export function lastUpdateMs(): number {
  return lastFleetUpdateMs;
}

// FAA rules — worst-of ceiling and visibility wins.
function deriveCategory(ceilingFt: number, visibilitySm: number): Category {
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

interface CloudLayer { base: number | null; amount: string | null; }

// api.weather.gov returns visibility in meters and cloud bases in meters;
// convert to statute miles and feet AGL for the FAA math.
const M_PER_SM = 1609.344;
const M_PER_FT = 0.3048;

function ceilingFromClouds(clouds: CloudLayer[] | null | undefined): number {
  if (!clouds) return Infinity;
  let ceiling = Infinity;
  for (const layer of clouds) {
    const amt = layer.amount?.toUpperCase();
    if (amt !== "BKN" && amt !== "OVC" && amt !== "VV") continue;
    const baseFt = (layer.base ?? Infinity) / M_PER_FT;
    if (baseFt < ceiling) ceiling = baseFt;
  }
  return ceiling;
}

interface NwsObservationProps {
  visibility?: { value: number | null };
  cloudLayers?: CloudLayer[];
}

interface NwsObservation {
  properties?: NwsObservationProps;
}

async function fetchStation(st: Station): Promise<void> {
  const url = `https://api.weather.gov/stations/${encodeURIComponent(st.icao)}/observations/latest`;
  const resp = await fetch(url, {
    headers: { Accept: "application/geo+json" },
  });
  if (!resp.ok) throw new Error(`weather ${st.icao}: HTTP ${resp.status}`);
  const doc = (await resp.json()) as NwsObservation;
  const props = doc.properties ?? {};
  const visMeters = props.visibility?.value;
  const visibilitySm =
    visMeters == null ? 10 : Math.min(10, Math.round(visMeters / M_PER_SM));
  const ceilingFt = ceilingFromClouds(props.cloudLayers);
  st.visibilitySm = visibilitySm;
  st.ceilingFtAgl = ceilingFt;
  st.category = deriveCategory(ceilingFt, visibilitySm);
  st.fetchedAtMs = Date.now();
}

/** Fire off one HTTP call per station in parallel. Individual failures
 *  leave that station's category as Unknown but don't reject the whole
 *  update. Populates the module-level cache. */
export async function updateAll(): Promise<void> {
  await Promise.all(
    STATIONS.map(async (st) => {
      try {
        await fetchStation(st);
      } catch (err) {
        console.warn(`weather ${st.icao}:`, err);
      }
    })
  );
  lastFleetUpdateMs = Date.now();
}

/** Fire an update only if data is missing or older than kTtlMs. */
export async function refreshIfStale(): Promise<void> {
  const ttlMs = 5 * 60 * 1000;
  const now = Date.now();
  if (lastFleetUpdateMs === 0 || now - lastFleetUpdateMs > ttlMs) {
    await updateAll();
  }
}
