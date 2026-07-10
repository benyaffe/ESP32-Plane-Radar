import { afterEach, describe, expect, it, vi } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import {
  _resetTileCache,
  fetchTile,
  loadTilesForView,
  RENDER_ZOOM,
  tileOf,
  tilesCovering,
} from "./tileFetch";

const SF_LAT = 37.7552;
const SF_LON = -122.4528;

// tileOf() / tilesCovering() are pure — locking them against the same
// numerical answers scripts/tile_scheme.py produces is the guarantee
// the browser asks for the same (z, x, y) files the pipeline bakes.

describe("tileOf", () => {
  it("puts Sutro Tower in tile (20, 37) at z=7 — matches Python", () => {
    // Same tile the emulator bootstrap bin was baked with.
    expect(tileOf(7, SF_LAT, SF_LON)).toEqual({ x: 20, y: 37 });
  });

  it("clamps exact ±90° latitudes into the edge tile row", () => {
    const north = tileOf(3, 90, 0);
    const south = tileOf(3, -90, 0);
    expect(north.y).toBe(0);
    expect(south.y).toBe((1 << 3) - 1);
  });

  it("wraps longitude past ±180 back onto the map", () => {
    // Both should land in the same tile as their +/- 360-shifted twin.
    const a = tileOf(5, 0, 179.9);
    const b = tileOf(5, 0, 179.9 - 360);
    expect(a).toEqual(b);
  });
});

describe("tilesCovering", () => {
  it("returns a single tile for a small radius well inside one", () => {
    // 10 km around Sutro is far from any tile boundary at z=7.
    const tiles = tilesCovering(7, SF_LAT, SF_LON, 10);
    expect(tiles).toEqual([{ z: 7, x: 20, y: 37 }]);
  });

  it("returns multiple tiles when the disc straddles a boundary", () => {
    // A big radius near the SF area straddles east/north neighbors.
    const tiles = tilesCovering(7, SF_LAT, SF_LON, 400);
    expect(tiles.length).toBeGreaterThan(1);
    for (const t of tiles) {
      expect(t.z).toBe(7);
      expect(t.x).toBeGreaterThanOrEqual(0);
      expect(t.y).toBeGreaterThanOrEqual(0);
    }
    // Center tile is always in the list.
    expect(tiles.some(t => t.x === 20 && t.y === 37)).toBe(true);
  });

  it("splits antimeridian-crossing views into east+west lookups", () => {
    // A huge disc centered near the dateline hits both the far-east and
    // far-west columns.
    const tiles = tilesCovering(5, 0, 179.5, 400);
    const xs = new Set(tiles.map(t => t.x));
    // z=5 has 32 columns; expect both x=0 and x=31 in the mix.
    expect(xs.has(0)).toBe(true);
    expect(xs.has((1 << 5) - 1)).toBe(true);
  });

  it("uses RENDER_ZOOM = 7 for the default render pass", () => {
    expect(RENDER_ZOOM).toBe(7);
  });
});

// Fetch client -----------------------------------------------------------
// The point of these tests is caching + 404-as-null, NOT the network.
// A stubbed fetch feeds the decoder real tile bytes so the whole chain
// including decodeTile() gets exercised together.

const SF_TILE_PATH = resolve(
  __dirname,
  "..", "..", "data", "emulator_bootstrap_tile_7_20_37.bin",
);
const SF_TILE_BYTES = new Uint8Array(readFileSync(SF_TILE_PATH));

function tileArrayBuffer(): ArrayBuffer {
  // Fresh copy each call so the test can't accidentally share buffer
  // state across cache-hit assertions.
  return SF_TILE_BYTES.slice().buffer;
}

function stubFetch(handler: (url: string) => Response | Promise<Response>): void {
  vi.stubGlobal("fetch", vi.fn((input: RequestInfo | URL) =>
    Promise.resolve(handler(String(input))),
  ));
}

afterEach(() => {
  vi.unstubAllGlobals();
  _resetTileCache();
});

describe("fetchTile", () => {
  it("decodes a fetched .bin into a Tile", async () => {
    stubFetch(() => new Response(tileArrayBuffer(), { status: 200 }));
    const t = await fetchTile({ z: 7, x: 20, y: 37 });
    expect(t).not.toBeNull();
    expect(t!.z).toBe(7);
    expect(t!.x).toBe(20);
    expect(t!.y).toBe(37);
  });

  it("returns null on a 404 rather than throwing", async () => {
    stubFetch(() => new Response("", { status: 404 }));
    const t = await fetchTile({ z: 7, x: 0, y: 0 });
    expect(t).toBeNull();
  });

  it("throws on other non-OK responses", async () => {
    stubFetch(() => new Response("boom", { status: 500 }));
    await expect(fetchTile({ z: 7, x: 20, y: 37 })).rejects.toThrow(/HTTP 500/);
  });

  it("hits network once per (z, x, y) — repeated calls hit the cache", async () => {
    const fetchSpy = vi.fn(() =>
      Promise.resolve(new Response(tileArrayBuffer(), { status: 200 })),
    );
    vi.stubGlobal("fetch", fetchSpy);
    await fetchTile({ z: 7, x: 20, y: 37 });
    await fetchTile({ z: 7, x: 20, y: 37 });
    await fetchTile({ z: 7, x: 20, y: 37 });
    expect(fetchSpy).toHaveBeenCalledTimes(1);
  });
});

describe("loadTilesForView", () => {
  it("filters out null (404) tiles from the returned list", async () => {
    // Alternate 200/404 so at least one tile drops.
    let call = 0;
    stubFetch(() => {
      call++;
      return call === 1
        ? new Response(tileArrayBuffer(), { status: 200 })
        : new Response("", { status: 404 });
    });
    const tiles = await loadTilesForView(SF_LAT, SF_LON, 400);
    expect(tiles.length).toBeGreaterThanOrEqual(1);
    for (const t of tiles) {
      expect(t.z).toBe(7);
    }
  });
});
