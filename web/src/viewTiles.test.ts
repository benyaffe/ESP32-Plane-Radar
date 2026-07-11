import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import type { Tile } from "./tile";

// These tests guard the tile-fetch supersede that caused the "blank
// radar at 10nm after home change" bug: a stale fetch for the old
// center must not overwrite fresh tiles for the new one.

// Mock loadTilesForView so we control the fetch timing exactly.
const loadCalls: Array<{
  lat: number; lon: number; radiusKm: number;
  resolve: (tiles: Tile[]) => void;
  reject: (err: Error) => void;
}> = [];

vi.mock("./tileFetch", () => ({
  loadTilesForView: vi.fn((lat: number, lon: number, radiusKm: number) =>
    new Promise<Tile[]>((resolve, reject) => {
      loadCalls.push({ lat, lon, radiusKm, resolve, reject });
    }),
  ),
}));

// Import AFTER vi.mock so the module picks up the mock.
const { ensureTiles, currentTiles, currentTilesKey, _resetViewTilesForTests } =
  await import("./viewTiles");

function fakeTile(x: number, y: number): Tile {
  // Shape doesn't matter for these tests — the module just stores it.
  return { z: 7, x, y, coast: [], land: [], airports: [] } as unknown as Tile;
}

beforeEach(() => {
  loadCalls.length = 0;
  _resetViewTilesForTests();
});

afterEach(() => {
  vi.clearAllMocks();
});

describe("ensureTiles — supersede on out-of-order resolves", () => {
  it("keeps the LAST-STARTED fetch's tiles when it resolves before the first", async () => {
    // Fetch A for Sutro, then fetch B for Yangon before A resolves.
    const pA = ensureTiles("radar", 37.755, -122.453, 20);
    const pB = ensureTiles("radar", 16.907, 96.133, 20);

    // Resolve B first, then A. If the supersede is missing, A's tiles
    // (last write wins on completion order) would clobber B's.
    const tilesB = [fakeTile(90, 55)];
    const tilesA = [fakeTile(20, 37)];
    loadCalls[1].resolve(tilesB);
    const bUpdated = await pB;
    loadCalls[0].resolve(tilesA);
    const aUpdated = await pA;

    expect(bUpdated).toBe(true);
    expect(aUpdated).toBe(false);  // superseded
    expect(currentTiles()).toEqual(tilesB);
    expect(currentTilesKey()).toContain("16.907");
  });

  it("joins the SAME-key in-flight promise instead of starting a duplicate", async () => {
    const { loadTilesForView } = await import("./tileFetch");
    const p1 = ensureTiles("radar", 37.755, -122.453, 20);
    const p2 = ensureTiles("radar", 37.755, -122.453, 20);
    expect((loadTilesForView as ReturnType<typeof vi.fn>).mock.calls.length).toBe(1);
    loadCalls[0].resolve([fakeTile(20, 37)]);
    await Promise.all([p1, p2]);
  });
});

describe("ensureTiles — empty result handling", () => {
  it("does NOT re-fetch when the tile set is unchanged for the same key", async () => {
    const { loadTilesForView } = await import("./tileFetch");
    const p1 = ensureTiles("radar", 37.755, -122.453, 20);
    loadCalls[0].resolve([fakeTile(20, 37)]);
    await p1;

    // Second call, same key — should short-circuit.
    const p2 = ensureTiles("radar", 37.755, -122.453, 20);
    expect((loadTilesForView as ReturnType<typeof vi.fn>).mock.calls.length).toBe(1);
    const updated = await p2;
    expect(updated).toBe(false);
  });

  it("does NOT loop-refetch when a legitimate empty result comes back", async () => {
    // The prior implementation had `if (key === tilesKey && tiles.length > 0)`
    // which meant an empty tile list (all-ocean disc) failed the guard and
    // re-entered the fetch path every notify. Regression check.
    const { loadTilesForView } = await import("./tileFetch");
    const p1 = ensureTiles("radar", 0, -170, 20);   // open ocean
    loadCalls[0].resolve([]);                        // legitimately empty
    await p1;

    const p2 = ensureTiles("radar", 0, -170, 20);
    expect((loadTilesForView as ReturnType<typeof vi.fn>).mock.calls.length).toBe(1);
    const updated = await p2;
    expect(updated).toBe(false);
  });
});

describe("ensureTiles — error path", () => {
  it("does not update tiles on a fetch error and lets a retry succeed", async () => {
    const p1 = ensureTiles("radar", 37.755, -122.453, 20);
    loadCalls[0].reject(new Error("boom"));
    const first = await p1;
    expect(first).toBe(false);
    expect(currentTiles()).toEqual([]);

    // Retry — no in-flight or key match, so a fresh fetch runs.
    const p2 = ensureTiles("radar", 37.755, -122.453, 20);
    loadCalls[1].resolve([fakeTile(20, 37)]);
    const second = await p2;
    expect(second).toBe(true);
    expect(currentTiles().length).toBe(1);
  });
});
