// Substring search over the compact airport_index payload.
// Used by the airport typeahead in web/src/settings.ts.

import type { AirportIndexRow } from "./data";
import { distanceNm } from "./weather";

// Case-insensitive substring match on ICAO, IATA, city, or name.
// Ranks exact prefix matches on ICAO/IATA first, then substring hits,
// then general name matches.
function scoreRow(row: AirportIndexRow, q: string): number {
  const [icao, iata, city, name] = row;
  const icaoL = icao.toLowerCase();
  const iataL = iata.toLowerCase();
  const cityL = city.toLowerCase();
  const nameL = name.toLowerCase();
  if (icaoL === q || iataL === q) return 1000;
  if (icaoL.startsWith(q) || iataL.startsWith(q)) return 500;
  if (cityL.startsWith(q) || nameL.startsWith(q)) return 200;
  if (icaoL.includes(q) || iataL.includes(q)) return 100;
  if (cityL.includes(q) || nameL.includes(q)) return 50;
  return 0;
}

export function search(index: AirportIndexRow[], query: string, limit = 8): AirportIndexRow[] {
  const q = query.trim().toLowerCase();
  if (q.length === 0) return [];
  const scored: [number, AirportIndexRow][] = [];
  for (const row of index) {
    const s = scoreRow(row, q);
    if (s > 0) scored.push([s, row]);
  }
  scored.sort((a, b) => b[0] - a[0]);
  return scored.slice(0, limit).map(([, row]) => row);
}

export interface NearestAirport {
  icao: string;
  lat: number;
  lon: number;
  distanceNm: number;
}

/** Nearest airport in the index whose IAP flag (column 6, 0/1) is 1.
 *  Returns null if the index has no IAP-capable entries. O(N) scan;
 *  cheap at the ~5-6k index size and once-per-second call cadence. */
export function nearestIapAirport(
  index: AirportIndexRow[],
  lat: number,
  lon: number,
): NearestAirport | null {
  let best: NearestAirport | null = null;
  for (const row of index) {
    const [icao, , , , rowLat, rowLon, iap] = row;
    if (iap !== 1) continue;
    const d = distanceNm(lat, lon, rowLat, rowLon);
    if (best === null || d < best.distanceNm) {
      best = { icao, lat: rowLat, lon: rowLon, distanceNm: d };
    }
  }
  return best;
}
