import { beforeEach, describe, expect, it } from "vitest";
import { applyUrlParams, updateShareUrl } from "./shareUrl";
import { state, DEFAULT_HOME, DEFAULT_METAR, setView } from "./state";
import type { AirportIndexRow } from "./data";

// Minimal test index — 5 rows across regions, all IAP-flagged.
const INDEX: AirportIndexRow[] = [
  ["KSFO", "SFO", "San Francisco", "San Francisco Intl", 37.6188, -122.3750, 1],
  ["KOAK", "OAK", "Oakland", "Oakland Intl", 37.7213, -122.2214, 1],
  ["EGLL", "LHR", "London", "London Heathrow", 51.4706, -0.4619, 1],
  ["PHNL", "HNL", "Honolulu", "Daniel K Inouye Intl", 21.3187, -157.9224, 1],
];

function setLocationSearch(search: string): void {
  const url = new URL(window.location.href);
  url.search = search;
  window.history.replaceState(null, "", url.pathname + url.search);
}

beforeEach(() => {
  // Reset in-memory state between tests.
  state.home = { ...DEFAULT_HOME };
  state.metar = { ...DEFAULT_METAR };
  state.centerLat = DEFAULT_HOME.lat;
  state.centerLon = DEFAULT_HOME.lon;
  state.centerLabel = "Home";
  state.focusIdx = 0;
  state.rangeIdx = 1;
  setView("radar");
  setLocationSearch("");
});

describe("applyUrlParams — read from URL", () => {
  it("centers on an ICAO code with ?center=KSFO", () => {
    setLocationSearch("?view=radar&center=KSFO&range=5");
    applyUrlParams(INDEX);
    expect(state.centerLabel).toBe("KSFO");
    expect(state.centerLat).toBeCloseTo(37.6188, 3);
    expect(state.rangeIdx).toBe(0);  // 5nm = idx 0
  });

  it("centers on lat,lon with ?center=37.75,-122.45", () => {
    setLocationSearch("?view=radar&center=37.75,-122.45&range=10");
    applyUrlParams(INDEX);
    expect(state.centerLat).toBeCloseTo(37.75, 4);
    expect(state.centerLon).toBeCloseTo(-122.45, 4);
    expect(state.rangeIdx).toBe(1);  // 10nm = idx 1
  });

  it("sets home from ?home=PHNL without persisting", () => {
    setLocationSearch("?view=cockpit&home=PHNL");
    applyUrlParams(INDEX);
    expect(state.home.lat).toBeCloseTo(21.3187, 3);
    expect(state.view).toBe("cockpit");
    // Confirm nothing was written to LS for the home key.
    expect(window.localStorage.getItem("planeradar.home")).toBeNull();
  });

  it("sets METAR from ?metar=lat,lon&rad=25 without persisting", () => {
    setLocationSearch("?view=weather&metar=37.5,-122.3&rad=25");
    applyUrlParams(INDEX);
    expect(state.metar.centerLat).toBeCloseTo(37.5, 3);
    expect(state.metar.radiusNm).toBe(25);
    expect(window.localStorage.getItem("planeradar.metar")).toBeNull();
  });

  it("ignores unknown ICAOs silently", () => {
    setLocationSearch("?view=radar&center=ZZZZ&range=5");
    applyUrlParams(INDEX);
    // Falls back to whatever state had — Home coords.
    expect(state.centerLabel).toBe("Home");
  });

  it("ignores out-of-range coordinates", () => {
    setLocationSearch("?view=radar&center=999,-999");
    applyUrlParams(INDEX);
    expect(state.centerLabel).toBe("Home");
  });
});

describe("updateShareUrl — write to URL", () => {
  it("writes ?view=radar&center=<icao>&range=<nm> on the radar view", () => {
    setView("radar");
    state.centerLabel = "KSFO";
    state.centerLat = 37.6188;
    state.centerLon = -122.375;
    state.rangeIdx = 2;  // 15nm
    updateShareUrl();
    const qs = new URLSearchParams(window.location.search);
    expect(qs.get("view")).toBe("radar");
    expect(qs.get("center")).toBe("KSFO");
    expect(qs.get("range")).toBe("15");
  });

  it("falls back to lat,lon when centerLabel isn't an ICAO code", () => {
    setView("radar");
    state.centerLabel = "Custom point";
    state.centerLat = 37.75;
    state.centerLon = -122.45;
    updateShareUrl();
    const qs = new URLSearchParams(window.location.search);
    expect(qs.get("center")).toBe("37.7500,-122.4500");
  });

  it("writes ?view=cockpit&home=lat,lon on the cockpit view", () => {
    state.home = { lat: 21.3187, lon: -157.9224 };
    setView("cockpit");
    updateShareUrl();
    const qs = new URLSearchParams(window.location.search);
    expect(qs.get("view")).toBe("cockpit");
    expect(qs.get("home")).toBe("21.3187,-157.9224");
  });

  it("writes ?view=weather&metar=<lat,lon>&rad=<nm> on the weather view", () => {
    state.metar = { centerLat: 37.5, centerLon: -122.3, radiusNm: 25 };
    setView("weather");
    updateShareUrl();
    const qs = new URLSearchParams(window.location.search);
    expect(qs.get("view")).toBe("weather");
    expect(qs.get("metar")).toBe("37.5000,-122.3000");
    expect(qs.get("rad")).toBe("25");
  });
});
