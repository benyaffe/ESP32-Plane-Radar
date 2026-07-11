// Client-side address lookup — talks to /api/geocode (Netlify proxy for
// Nominatim). Debounced, in-memory LRU-cached, and abort-safe so the
// address typeahead stays responsive.
//
// Trigger rules live at the call site (settings.ts): fire only when
// the query is >= 4 chars, debounce 300 ms, and pass the previous
// call's AbortController into `lookup()` so the in-flight request
// gets cancelled the moment the user types again.

export interface GeocodeHit {
  displayName: string;
  lat: number;
  lon: number;
}

const CACHE_LIMIT = 50;
// Insertion order = LRU tail. On hit we delete+set to move to tail.
const cache = new Map<string, GeocodeHit[]>();

function cacheGet(q: string): GeocodeHit[] | undefined {
  const hit = cache.get(q);
  if (!hit) return undefined;
  cache.delete(q);
  cache.set(q, hit);
  return hit;
}

function cacheSet(q: string, hits: GeocodeHit[]): void {
  cache.set(q, hits);
  while (cache.size > CACHE_LIMIT) {
    const oldest = cache.keys().next().value;
    if (oldest === undefined) break;
    cache.delete(oldest);
  }
}

/** Query the geocode proxy. Returns [] on abort, network error, or a
 *  malformed response. Cached in-process. */
export async function lookup(query: string, signal?: AbortSignal): Promise<GeocodeHit[]> {
  const q = query.trim();
  if (q.length < 4) return [];
  const cached = cacheGet(q);
  if (cached) return cached;
  try {
    const resp = await fetch(`/api/geocode?q=${encodeURIComponent(q)}`, { signal });
    if (!resp.ok) return [];
    const rows = (await resp.json()) as unknown;
    if (!Array.isArray(rows)) return [];
    const hits: GeocodeHit[] = [];
    for (const r of rows) {
      if (typeof r !== "object" || r === null) continue;
      const row = r as { display_name?: unknown; lat?: unknown; lon?: unknown };
      if (typeof row.display_name !== "string") continue;
      if (typeof row.lat !== "number" || typeof row.lon !== "number") continue;
      hits.push({ displayName: row.display_name, lat: row.lat, lon: row.lon });
    }
    cacheSet(q, hits);
    return hits;
  } catch {
    // Aborted, offline, or upstream error — silent fallback keeps the
    // airport-search half of the typeahead working.
    return [];
  }
}

// Exposed for tests only; safe to call anytime.
export function _clearCache(): void {
  cache.clear();
}
