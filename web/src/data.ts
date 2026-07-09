// Fetch + cache the baked JSON payloads emitted by
// scripts/build_web_data.py.

export type LonLat = [number, number];

export interface LandData {
  vertices: LonLat[];
  triangles: [number, number, number][];
}

export interface RoadLine {
  type: string;         // "Major Highway" | "Secondary Highway"
  points: LonLat[];
}

export interface Runway {
  le: string;
  he: string;
  lat1: number; lon1: number;
  lat2: number; lon2: number;
}

export interface Airport {
  name: string;
  city: string;
  lat: number;
  lon: number;
  tier: number;         // 3=large, 2=medium, 1=small
  runways: Runway[];
}

// Compact typeahead index: [icao, iata, city, name, lat, lon]
export type AirportIndexRow = [string, string, string, string, number, number];

export interface MapData {
  coastline: LonLat[][];
  land: LandData;
  roads: RoadLine[];
  airports: Record<string, Airport>;
  airportIndex: AirportIndexRow[];
}

// Naive cache: fetch once per URL. Replace with a per-region cache when
// dynamic CONUS lands.
const cache = new Map<string, Promise<unknown>>();

async function fetchJSON<T>(url: string): Promise<T> {
  const existing = cache.get(url);
  if (existing) return existing as Promise<T>;
  const p = fetch(url).then(async (r) => {
    if (!r.ok) throw new Error(`fetch ${url}: HTTP ${r.status}`);
    return r.json() as Promise<T>;
  });
  cache.set(url, p);
  return p;
}

export async function loadMapData(basePath = "data"): Promise<MapData> {
  const [coastline, land, roads, airports, airportIndex] = await Promise.all([
    fetchJSON<LonLat[][]>(`${basePath}/coastline.json`),
    fetchJSON<LandData>(`${basePath}/land.json`),
    fetchJSON<RoadLine[]>(`${basePath}/roads.json`),
    fetchJSON<Record<string, Airport>>(`${basePath}/airports.json`),
    fetchJSON<AirportIndexRow[]>(`${basePath}/airport_index.json`),
  ]);
  return { coastline, land, roads, airports, airportIndex };
}
