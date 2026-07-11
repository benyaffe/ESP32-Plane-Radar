// @vitest-environment happy-dom
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

// state.ts reads localStorage on module init and uses `window.localStorage`
// throughout, so we need a DOM env. happy-dom provides `window` but not
// `localStorage` in this vitest version, so we ship a small in-memory
// shim and attach it to window before importing state.

function makeLocalStorageShim(): Storage {
  const store = new Map<string, string>();
  return {
    get length() { return store.size; },
    clear() { store.clear(); },
    getItem(k: string) { return store.get(k) ?? null; },
    setItem(k: string, v: string) { store.set(k, String(v)); },
    removeItem(k: string) { store.delete(k); },
    key(i: number) { return Array.from(store.keys())[i] ?? null; },
  };
}

async function freshState() {
  (window as unknown as { localStorage: Storage }).localStorage = makeLocalStorageShim();
  vi.resetModules();
  return await import("./state");
}

beforeEach(() => {
  (window as unknown as { localStorage: Storage }).localStorage = makeLocalStorageShim();
  vi.resetModules();
});

afterEach(() => {
  (window as unknown as { localStorage: Storage | undefined }).localStorage = undefined;
});

describe("initial load", () => {
  it("boots with DEFAULT_HOME/METAR/focus when localStorage is empty", async () => {
    const { state, DEFAULT_HOME, DEFAULT_METAR, DEFAULT_FOCUS_RING } = await freshState();
    expect(state.home).toEqual(DEFAULT_HOME);
    expect(state.metar).toEqual(DEFAULT_METAR);
    expect(state.focusRing.length).toBe(DEFAULT_FOCUS_RING.length);
    expect(state.focusRing[0].isHome).toBe(true);
    expect(state.focusIdx).toBe(0);
    expect(state.view).toBe("radar");
    expect(state.centerLat).toBe(DEFAULT_HOME.lat);
    expect(state.centerLon).toBe(DEFAULT_HOME.lon);
  });

  it("restores home from localStorage", async () => {
    window.localStorage.setItem("planeradar.home",
      JSON.stringify({ lat: 16.9073, lon: 96.1332 }));
    vi.resetModules();
    const { state } = await import("./state");
    expect(state.home).toEqual({ lat: 16.9073, lon: 96.1332 });
    // Home slot in the ring should reflect the loaded home.
    expect(state.focusRing[0].lat).toBe(16.9073);
    expect(state.focusRing[0].lon).toBe(96.1332);
  });

  it("falls back to DEFAULT_HOME when the stored value is invalid", async () => {
    window.localStorage.setItem("planeradar.home", "not-json");
    vi.resetModules();
    const { state, DEFAULT_HOME } = await import("./state");
    expect(state.home).toEqual(DEFAULT_HOME);
  });
});

describe("saveHome", () => {
  it("updates state.home and the home-slot in focusRing", async () => {
    const { state, saveHome } = await freshState();
    saveHome({ lat: 39.4991, lon: -119.768 });
    expect(state.home).toEqual({ lat: 39.4991, lon: -119.768 });
    const homeSlot = state.focusRing.find(fp => fp.isHome)!;
    expect(homeSlot.lat).toBe(39.4991);
    expect(homeSlot.lon).toBe(-119.768);
  });

  it("re-centers immediately when the current focus IS the home slot", async () => {
    const { state, saveHome } = await freshState();
    expect(state.focusRing[state.focusIdx].isHome).toBe(true);
    saveHome({ lat: 39.4991, lon: -119.768 });
    expect(state.centerLat).toBe(39.4991);
    expect(state.centerLon).toBe(-119.768);
  });

  it("does NOT re-center when the current focus is not the home slot", async () => {
    const { state, saveHome, cycleFocus } = await freshState();
    cycleFocus();                              // move off Home to SFO
    expect(state.focusRing[state.focusIdx].isHome).toBe(false);
    const sfoLat = state.centerLat;
    const sfoLon = state.centerLon;
    saveHome({ lat: 39.4991, lon: -119.768 });
    // Center still on SFO — the fresh home coords sit on the ring and
    // take effect next time the tap-cycle lands on Home.
    expect(state.centerLat).toBe(sfoLat);
    expect(state.centerLon).toBe(sfoLon);
  });

  it("persists to localStorage", async () => {
    const { saveHome } = await freshState();
    saveHome({ lat: 39.4991, lon: -119.768 });
    const stored = JSON.parse(window.localStorage.getItem("planeradar.home")!);
    expect(stored).toEqual({ lat: 39.4991, lon: -119.768 });
  });

  it("notifies subscribers exactly once", async () => {
    const { subscribe, saveHome } = await freshState();
    const spy = vi.fn();
    subscribe(spy);
    saveHome({ lat: 39.4991, lon: -119.768 });
    expect(spy).toHaveBeenCalledTimes(1);
  });
});

describe("cycleFocus", () => {
  it("walks the ring and updates centerLat/Lon/Label/rangeIdx", async () => {
    const { state, cycleFocus } = await freshState();
    const ring = state.focusRing;
    expect(state.focusIdx).toBe(0);
    cycleFocus();
    expect(state.focusIdx).toBe(1);
    expect(state.centerLat).toBe(ring[1].lat);
    expect(state.centerLon).toBe(ring[1].lon);
    expect(state.centerLabel).toBe(ring[1].label);
    expect(state.rangeIdx).toBe(ring[1].defaultRangeIdx);
  });

  it("wraps back to focusIdx=0 at the end", async () => {
    const { state, cycleFocus } = await freshState();
    const n = state.focusRing.length;
    for (let i = 0; i < n; i++) cycleFocus();
    expect(state.focusIdx).toBe(0);
  });
});

describe("setViewAndFocus", () => {
  it("switches view AND focus in one call, notifying exactly once", async () => {
    const { state, subscribe, setViewAndFocus } = await freshState();
    const spy = vi.fn();
    subscribe(spy);
    setViewAndFocus("radar", 1);
    expect(spy).toHaveBeenCalledTimes(1);
    expect(state.view).toBe("radar");
    expect(state.focusIdx).toBe(1);
    expect(state.centerLat).toBe(state.focusRing[1].lat);
    expect(state.centerLabel).toBe(state.focusRing[1].label);
    expect(state.rangeIdx).toBe(state.focusRing[1].defaultRangeIdx);
  });

  it("changes view only when focusIdx is out of range", async () => {
    const { state, setViewAndFocus, setView } = await freshState();
    setView("weather");
    const beforeFocusIdx = state.focusIdx;
    setViewAndFocus("radar", 99);
    expect(state.view).toBe("radar");
    expect(state.focusIdx).toBe(beforeFocusIdx);  // untouched
  });
});

describe("resetAllSettings", () => {
  it("clears all four localStorage keys", async () => {
    const { saveHome, saveMetar, saveFocusRing, state, resetAllSettings } = await freshState();
    saveHome({ lat: 39.4991, lon: -119.768 });
    saveMetar({ centerLat: 39.5, centerLon: -119.8, radiusNm: 30 });
    saveFocusRing(state.focusRing);
    expect(window.localStorage.getItem("planeradar.home")).toBeTruthy();
    expect(window.localStorage.getItem("planeradar.metar")).toBeTruthy();
    expect(window.localStorage.getItem("planeradar.focusRing")).toBeTruthy();
    // resetAllSettings reloads the page — stub location.reload to observe.
    const reloadSpy = vi.fn();
    Object.defineProperty(window, "location", {
      value: { ...window.location, reload: reloadSpy },
      writable: true,
    });
    resetAllSettings();
    expect(window.localStorage.getItem("planeradar.home")).toBeNull();
    expect(window.localStorage.getItem("planeradar.metar")).toBeNull();
    expect(window.localStorage.getItem("planeradar.focusRing")).toBeNull();
    expect(reloadSpy).toHaveBeenCalled();
  });
});
