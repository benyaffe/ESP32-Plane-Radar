// Owns the "which tiles are loaded for the current view" state, with a
// generation-counter supersede so out-of-order fetch results can't
// clobber fresh tiles. Split out of main.ts so it can be unit-tested
// without pulling the DOM/subscriber wiring along.

import { loadTilesForView } from "./tileFetch";
import type { Tile } from "./tile";

let tiles: Tile[] = [];
let tilesKey = "";
let tilesRequestId = 0;
let tilesInFlightKey: string | null = null;
let tilesInFlightPromise: Promise<boolean> | null = null;

export function currentTiles(): Tile[] {
  return tiles;
}

export function currentTilesKey(): string {
  return tilesKey;
}

// Fetch tiles for the given view+geometry if we haven't already. Returns
// true if this call caused `tiles` to update (so the caller can trigger
// a repaint), false if it was a no-op or superseded.
export async function ensureTiles(
  viewTag: string,
  lat: number,
  lon: number,
  radiusKm: number,
): Promise<boolean> {
  const key = `${viewTag}:${lat.toFixed(3)}:${lon.toFixed(3)}:${radiusKm.toFixed(1)}`;
  // NOTE: no `tiles.length > 0` guard — a legitimate empty result (e.g.,
  // an all-ocean disc) is valid; re-checking would loop-refetch forever.
  if (key === tilesKey) return false;
  if (tilesInFlightKey === key && tilesInFlightPromise) return tilesInFlightPromise;
  const myId = ++tilesRequestId;
  tilesInFlightKey = key;
  tilesInFlightPromise = (async () => {
    try {
      const next = await loadTilesForView(lat, lon, radiusKm);
      if (myId !== tilesRequestId) return false;
      tiles = next;
      tilesKey = key;
      return true;
    } catch (err) {
      console.error("tile fetch failed", err);
      return false;
    } finally {
      if (myId === tilesRequestId) {
        tilesInFlightKey = null;
        tilesInFlightPromise = null;
      }
    }
  })();
  return tilesInFlightPromise;
}

// Test-only: wipe the module-scoped state so each test starts clean.
// Not exported from the barrel; tests import directly.
export function _resetViewTilesForTests(): void {
  tiles = [];
  tilesKey = "";
  tilesRequestId = 0;
  tilesInFlightKey = null;
  tilesInFlightPromise = null;
}
