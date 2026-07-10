// Tile fetch client + viewport-to-tile math.
//
// Mirrors scripts/tile_scheme.py's tile_of() / tiles_covering() so the
// browser asks for the same (z, x, y) files the Python pipeline bakes
// and Netlify serves under /data/tiles/{z}/{x}/{y}.bin.
//
// Fetches are keyed on "z/x/y"; in-flight and completed decodes are
// cached in a single Map so panning across a boundary doesn't refetch
// tiles already in memory. Nothing evicts today — a browser session
// holds at most a few dozen z=7 tiles even for a heavy pan session,
// and each is at most a few hundred KB.

import { decodeTile, type Tile } from "./tile";

export const RENDER_ZOOM = 7;

export interface TileId {
  z: number;
  x: number;
  y: number;
}

export function tilesPerSide(z: number): number {
  if (z < 0) throw new Error(`zoom must be >= 0, got ${z}`);
  return 1 << z;
}

/** Which tile at zoom z contains the given point. Latitudes clamped
 *  just inside the poles; longitudes wrapped to (-180, 180]. Matches
 *  scripts/tile_scheme.py::tile_of() byte-for-byte. */
export function tileOf(z: number, lat: number, lon: number): { x: number; y: number } {
  const n = tilesPerSide(z);
  const clampedLat = Math.max(-90 + 1e-9, Math.min(90 - 1e-9, lat));
  const wrappedLon = ((lon + 180) % 360 + 360) % 360 - 180;
  let x = Math.floor((wrappedLon + 180) / (360 / n));
  let y = Math.floor((90 - clampedLat) / (180 / n));
  x = Math.max(0, Math.min(n - 1, x));
  y = Math.max(0, Math.min(n - 1, y));
  return { x, y };
}

/** Every (x, y) tile at zoom z whose bounds touch the disc of `radiusKm`
 *  around (centerLat, centerLon). Antimeridian crossings split into two
 *  east/west queries. Mirrors scripts/tile_scheme.py::tiles_covering(). */
export function tilesCovering(
  z: number,
  centerLat: number,
  centerLon: number,
  radiusKm: number,
): TileId[] {
  const kmPerDegLat = 111.0;
  const kmPerDegLon = 111.0 * Math.max(0.05, Math.cos(centerLat * Math.PI / 180));
  const dlat = radiusKm / kmPerDegLat;
  const dlon = radiusKm / kmPerDegLon;
  const minLat = Math.max(-89.999, centerLat - dlat);
  const maxLat = Math.min(89.999, centerLat + dlat);
  const lonWest = centerLon - dlon;
  const lonEast = centerLon + dlon;

  const ranges: Array<[number, number]> = [];
  if (lonWest < -180) {
    ranges.push([lonWest + 360, 180]);
    ranges.push([-180, lonEast]);
  } else if (lonEast > 180) {
    ranges.push([lonWest, 180]);
    ranges.push([-180, lonEast - 360]);
  } else {
    ranges.push([lonWest, lonEast]);
  }

  const seen = new Set<string>();
  const result: TileId[] = [];
  for (const [lonLo, lonHi] of ranges) {
    const lo = tileOf(z, minLat, lonLo);
    // Nudge east edge just inside 180° so the wrap doesn't jump to x=0.
    const hi = tileOf(z, maxLat, Math.min(lonHi, 180 - 1e-9));
    for (let x = lo.x; x <= hi.x; x++) {
      for (let y = hi.y; y <= lo.y; y++) {
        const key = `${x},${y}`;
        if (seen.has(key)) continue;
        seen.add(key);
        result.push({ z, x, y });
      }
    }
  }
  return result;
}

// Cache and fetch --------------------------------------------------------

const cache = new Map<string, Promise<Tile | null>>();

/** For tests. Not exported from the barrel. */
export function _resetTileCache(): void {
  cache.clear();
}

function keyOf(id: TileId): string {
  return `${id.z}/${id.x}/${id.y}`;
}

async function doFetch(id: TileId, basePath: string): Promise<Tile | null> {
  const url = `${basePath}/tiles/${id.z}/${id.x}/${id.y}.bin`;
  const res = await fetch(url);
  if (!res.ok) {
    // 404 is expected for open-ocean / uninhabited tiles the baker skips;
    // treat it as "nothing to draw here" rather than an error.
    if (res.status === 404) return null;
    throw new Error(`fetch ${url}: HTTP ${res.status}`);
  }
  const buf = await res.arrayBuffer();
  return decodeTile(buf);
}

/** Fetch + decode a single tile, memoized by (z, x, y). Returns null
 *  when the tile file doesn't exist (e.g., open ocean). */
export function fetchTile(id: TileId, basePath = "data"): Promise<Tile | null> {
  const k = keyOf(id);
  const existing = cache.get(k);
  if (existing) return existing;
  const p = doFetch(id, basePath).catch((err) => {
    // On real fetch error, drop the cache entry so the next attempt
    // can retry instead of being locked to a rejected promise.
    cache.delete(k);
    throw err;
  });
  cache.set(k, p);
  return p;
}

/** Fetch every tile covering the viewport, in parallel. Returns only
 *  the tiles that exist (null-tiles are filtered out). */
export async function loadTilesForView(
  centerLat: number,
  centerLon: number,
  radiusKm: number,
  basePath = "data",
): Promise<Tile[]> {
  const ids = tilesCovering(RENDER_ZOOM, centerLat, centerLon, radiusKm);
  const settled = await Promise.all(ids.map(id => fetchTile(id, basePath)));
  return settled.filter((t): t is Tile => t !== null);
}
