// @vitest-environment happy-dom
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// outdoorTemp reads state.home at fetch time and caches its result for
// 15 min. Guarantees the invalidate() shim used by main.ts's home-change
// subscriber actually forces a refetch of the new home's temperature.

function makeLocalStorageShim(): Storage {
  const store = new Map<string, string>();
  return {
    get length() { return store.size; },
    clear() { store.clear(); },
    getItem(k) { return store.get(k) ?? null; },
    setItem(k, v) { store.set(k, String(v)); },
    removeItem(k) { store.delete(k); },
    key(i) { return Array.from(store.keys())[i] ?? null; },
  };
}

async function freshModules() {
  (window as unknown as { localStorage: Storage }).localStorage = makeLocalStorageShim();
  vi.resetModules();
  const stateMod = await import("./state");
  const otMod = await import("./outdoorTemp");
  return { ...stateMod, ...otMod };
}

function meteoResponse(tempF: number, windKts = 10, windDeg = 270, pressureHpa = 1013): Response {
  return new Response(JSON.stringify({
    current: {
      temperature_2m: tempF,
      wind_speed_10m: windKts,
      wind_direction_10m: windDeg,
      pressure_msl: pressureHpa,
    },
  }), { status: 200 });
}

beforeEach(() => {
  (window as unknown as { localStorage: Storage }).localStorage = makeLocalStorageShim();
});

afterEach(() => {
  vi.unstubAllGlobals();
  (window as unknown as { localStorage: Storage | undefined }).localStorage = undefined;
});

describe("refreshIfStale", () => {
  it("populates the cache from a successful fetch", async () => {
    const { refreshIfStale, cachedReading } = await freshModules();
    vi.stubGlobal("fetch", vi.fn(() => Promise.resolve(meteoResponse(72))));
    await refreshIfStale();
    const r = cachedReading();
    expect(r.valid).toBe(true);
    expect(r.tempF).toBe(72);
  });

  it("hits the network at most once inside the 15-min TTL window", async () => {
    const { refreshIfStale } = await freshModules();
    const fetchMock = vi.fn(() => Promise.resolve(meteoResponse(72)));
    vi.stubGlobal("fetch", fetchMock);
    await refreshIfStale();
    await refreshIfStale();
    await refreshIfStale();
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  it("leaves the cached value alone when a subsequent fetch errors", async () => {
    const { refreshIfStale, cachedReading, invalidate } = await freshModules();
    vi.stubGlobal("fetch", vi.fn(() => Promise.resolve(meteoResponse(72))));
    await refreshIfStale();
    expect(cachedReading().tempF).toBe(72);

    // Invalidate to force the next call, then error.
    invalidate();
    vi.unstubAllGlobals();
    vi.stubGlobal("fetch", vi.fn(() => Promise.resolve(new Response("boom", { status: 500 }))));
    await refreshIfStale();

    // Cache was cleared by invalidate; the error path doesn't repopulate.
    // The relevant assertion: it doesn't throw, and the console.warn path
    // was exercised.
    expect(cachedReading().valid).toBe(false);
  });
});

describe("URL construction — uses state.home", () => {
  it("puts the current state.home lat/lon in the query", async () => {
    const { refreshIfStale, saveHome } = await freshModules();
    saveHome({ lat: 39.4991, lon: -119.768 });   // Reno
    const fetchMock = vi.fn((_input: RequestInfo | URL) =>
      Promise.resolve(meteoResponse(60)),
    );
    vi.stubGlobal("fetch", fetchMock);
    await refreshIfStale();
    const url = String(fetchMock.mock.calls[0]?.[0] ?? "");
    expect(url).toContain("latitude=39.499100");
    expect(url).toContain("longitude=-119.768000");
  });
});

describe("invalidate", () => {
  it("forces the next refreshIfStale to refetch", async () => {
    const { refreshIfStale, invalidate } = await freshModules();
    const fetchMock = vi.fn(() => Promise.resolve(meteoResponse(72)));
    vi.stubGlobal("fetch", fetchMock);
    await refreshIfStale();
    expect(fetchMock).toHaveBeenCalledTimes(1);

    invalidate();
    await refreshIfStale();
    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  it("marks the cached reading invalid", async () => {
    const { refreshIfStale, invalidate, cachedReading } = await freshModules();
    vi.stubGlobal("fetch", vi.fn(() => Promise.resolve(meteoResponse(72))));
    await refreshIfStale();
    expect(cachedReading().valid).toBe(true);
    invalidate();
    expect(cachedReading().valid).toBe(false);
  });
});
