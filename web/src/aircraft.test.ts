import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import {
  aircraft,
  clearAircraft,
  deduplicateGhosts,
  fetchAircraft,
  fetchCount,
  lastError,
} from "./aircraft";
import type { Aircraft } from "./aircraft";

// These tests guard the fetch-supersede race that caused the "no
// airplanes after home change" bug: two fetches for different centers
// running concurrently must not clobber each other, and clearAircraft()
// must drop the cached list so old-center planes don't linger.

interface DeferredResponse {
  resolve: (r: Response) => void;
  reject: (err: Error) => void;
}

function deferredFetch(): {
  fetchMock: ReturnType<typeof vi.fn>;
  pending: DeferredResponse[];
} {
  const pending: DeferredResponse[] = [];
  const fetchMock = vi.fn(() =>
    new Promise<Response>((resolve, reject) => {
      pending.push({ resolve, reject });
    }),
  );
  vi.stubGlobal("fetch", fetchMock);
  return { fetchMock, pending };
}

function adsbResponse(planes: Array<{ hex: string; lat: number; lon: number }>): Response {
  return new Response(JSON.stringify({ ac: planes }), { status: 200 });
}

beforeEach(() => {
  // Reset module state — each test starts with an empty cache.
  clearAircraft();
});

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("fetchAircraft — generation-counter supersede", () => {
  it("keeps only the LAST-STARTED fetch's payload when resolved out of order", async () => {
    const { pending } = deferredFetch();

    // Kick off two overlapping fetches, second one for a different center.
    const p1 = fetchAircraft(37.75, -122.45, 11);   // Sutro
    const p2 = fetchAircraft(16.90, 96.13, 11);     // Yangon

    // Resolve OUT OF ORDER — Sutro (first) arrives after Yangon (second).
    pending[1].resolve(adsbResponse([{ hex: "YGN1", lat: 16.9, lon: 96.1 }]));
    await p2;

    pending[0].resolve(adsbResponse([{ hex: "SUT1", lat: 37.7, lon: -122.4 }]));
    await p1;

    // The stale Sutro response must NOT overwrite the fresh Yangon list.
    const list = aircraft();
    expect(list.length).toBe(1);
    expect(list[0].hex).toBe("YGN1");
  });

  it("does not increment fetchCount for a superseded fetch", async () => {
    const { pending } = deferredFetch();
    const startCount = fetchCount();

    const p1 = fetchAircraft(37.75, -122.45, 11);
    const p2 = fetchAircraft(16.90, 96.13, 11);

    pending[1].resolve(adsbResponse([{ hex: "YGN1", lat: 16.9, lon: 96.1 }]));
    await p2;
    pending[0].resolve(adsbResponse([{ hex: "SUT1", lat: 37.7, lon: -122.4 }]));
    await p1;

    // Only the winning (second-started) fetch bumps the count.
    expect(fetchCount()).toBe(startCount + 1);
  });
});

describe("fetchAircraft — error handling", () => {
  it("preserves the last-good list when a subsequent fetch returns non-OK", async () => {
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(adsbResponse([{ hex: "OK1", lat: 37.7, lon: -122.4 }])),
    ));
    await fetchAircraft(37.75, -122.45, 11);
    expect(aircraft().length).toBe(1);

    // Second call errors out.
    vi.unstubAllGlobals();
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(new Response("boom", { status: 500 })),
    ));
    await fetchAircraft(37.75, -122.45, 11);

    // Old list is retained so the UI doesn't blank on a transient error.
    expect(aircraft().length).toBe(1);
    expect(aircraft()[0].hex).toBe("OK1");
    expect(lastError()).toMatch(/HTTP 500/);
  });

  it("preserves the last-good list on a thrown exception", async () => {
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(adsbResponse([{ hex: "OK1", lat: 37.7, lon: -122.4 }])),
    ));
    await fetchAircraft(37.75, -122.45, 11);
    expect(aircraft().length).toBe(1);

    vi.unstubAllGlobals();
    vi.stubGlobal("fetch", vi.fn(() => Promise.reject(new Error("net down"))));
    await fetchAircraft(37.75, -122.45, 11);

    expect(aircraft().length).toBe(1);
    expect(lastError()).toMatch(/net down/);
  });
});

describe("clearAircraft", () => {
  it("empties the cached list", async () => {
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(adsbResponse([{ hex: "OK1", lat: 37.7, lon: -122.4 }])),
    ));
    await fetchAircraft(37.75, -122.45, 11);
    expect(aircraft().length).toBe(1);
    clearAircraft();
    expect(aircraft().length).toBe(0);
    expect(lastError()).toBeNull();
  });

  it("causes an in-flight fetch to discard its result", async () => {
    const { pending } = deferredFetch();

    const p = fetchAircraft(37.75, -122.45, 11);
    clearAircraft();
    // The in-flight fetch's gen no longer matches — its response must not
    // resurrect the (now cleared) aircraft list.
    pending[0].resolve(adsbResponse([{ hex: "STALE", lat: 37.7, lon: -122.4 }]));
    await p;

    expect(aircraft().length).toBe(0);
  });
});

describe("URL construction", () => {
  it("includes lat/lon/nm in the /api/adsb query", async () => {
    const fetchMock = vi.fn((_input: RequestInfo | URL) =>
      Promise.resolve(adsbResponse([])),
    );
    vi.stubGlobal("fetch", fetchMock);
    await fetchAircraft(16.9073, 96.1332, 11);
    const url = String(fetchMock.mock.calls[0]?.[0] ?? "");
    expect(url).toMatch(/api\/adsb/);
    expect(url).toContain("lat=16.9073");
    expect(url).toContain("lon=96.1332");
    expect(url).toContain("nm=11.0");
  });
});

describe("deduplicateGhosts", () => {
  function make(overrides: Partial<Aircraft>): Aircraft {
    return {
      hex: "AAA",
      callsign: "",
      reg: "",
      type: "",
      lat: 37.0,
      lon: -122.0,
      altFt: 10_000,
      gsKnots: 250,
      trackDeg: 90,
      noseDeg: 90,
      vsFpm: 0,
      squawk: 1200,
      sourceTier: 3,
      ...overrides,
    };
  }

  it("drops a no-identity TIS-B ghost co-located with an ADS-B track", () => {
    const kept = make({ hex: "a704c0", callsign: "UAL1234", sourceTier: 3 });
    const ghost = make({ hex: "~270c06", callsign: "", reg: "", sourceTier: 0,
      lat: 37.001 });  // ~110 m north
    const out = deduplicateGhosts([kept, ghost]);
    expect(out).toHaveLength(1);
    expect(out[0].callsign).toBe("UAL1234");
  });

  it("drops a matching-callsign MLAT track when co-located with an ADS-R twin", () => {
    const ads_r = make({ hex: "ad138e", callsign: "N9412S", reg: "N9412S", sourceTier: 2 });
    const mlat = make({ hex: "100000", callsign: "N9412S", reg: "", sourceTier: 1,
      lat: 37.0002, lon: -122.0002 });
    const out = deduplicateGhosts([ads_r, mlat]);
    expect(out).toHaveLength(1);
    expect(out[0].sourceTier).toBe(2);
  });

  it("keeps both when callsigns and registrations differ (approach queue)", () => {
    const a = make({ callsign: "B08", reg: "N445XB", sourceTier: 2 });
    const b = make({ callsign: "DAL449", reg: "N874DN", sourceTier: 3,
      lat: 37.001, lon: -122.001 });
    const out = deduplicateGhosts([a, b]);
    expect(out).toHaveLength(2);
  });

  it("keeps same-tier neighbors even if they are essentially on top of each other", () => {
    const a = make({ callsign: "FLT1", sourceTier: 3 });
    const b = make({ callsign: "FLT2", sourceTier: 3,
      lat: 37.0005, lon: -122.0005 });
    const out = deduplicateGhosts([a, b]);
    expect(out).toHaveLength(2);
  });

  it("keeps both when the pair is far apart even with matching identity", () => {
    const a = make({ callsign: "N9412S", sourceTier: 3 });
    const b = make({ callsign: "N9412S", sourceTier: 1, lat: 37.1 });  // ~11 km
    const out = deduplicateGhosts([a, b]);
    expect(out).toHaveLength(2);
  });

  it("normalizes the ~ prefix before comparing identities", () => {
    const a = make({ callsign: "N9412S", sourceTier: 3 });
    const b = make({ callsign: "~N9412S", sourceTier: 1,
      lat: 37.0001, lon: -122.0001 });
    const out = deduplicateGhosts([a, b]);
    expect(out).toHaveLength(1);
  });
});

describe("callsign fallback", () => {
  async function fetchOne(raw: Record<string, unknown>): Promise<void> {
    vi.stubGlobal("fetch", vi.fn(() =>
      Promise.resolve(new Response(
        JSON.stringify({ ac: [{ lat: 37.7, lon: -122.4, ...raw }] }),
        { status: 200 },
      )),
    ));
    await fetchAircraft(37.75, -122.45, 11);
  }

  it("prefers flight over registration and hex", async () => {
    await fetchOne({ hex: "ABC123", flight: "UAL1234 ", r: "N12345" });
    expect(aircraft()[0].callsign).toBe("UAL1234");
  });

  it("falls back to registration when flight is missing", async () => {
    await fetchOne({ hex: "ABC123", r: "N12345" });
    expect(aircraft()[0].callsign).toBe("N12345");
  });

  it("leaves callsign empty for hex-only tracks (TIS-B / MLAT ghosts)", async () => {
    // The old behavior uppercased the hex and displayed it as a callsign.
    // adsb.fi's ~-prefixed synthetic ids read as garbage on the device,
    // so we now leave callsign empty and let the render layer suppress
    // the tag entirely.
    await fetchOne({ hex: "~2bb34b" });
    expect(aircraft()[0].callsign).toBe("");
    expect(aircraft()[0].hex).toBe("~2bb34b");  // hex still populated as identity key
  });
});
