import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { nearestIapAirport, search } from "./airports";
import type { AirportIndexRow } from "./data";

// The typeahead in Settings ("Center on airport", focus-ring Label
// cells) shells out to search() over airport_index.json. This file
// used to bake US-only ("K"-prefixed ICAOs) and typing "EGLL" or "LHR"
// silently returned no matches. Guards the reverted filter — a future
// regression would break the global typeahead again.

const INDEX_PATH = resolve(
  __dirname, "..", "public", "data", "airport_index.json",
);
const index: AirportIndexRow[] = JSON.parse(readFileSync(INDEX_PATH, "utf-8"));

describe("airport_index.json global coverage", () => {
  it("includes Heathrow by ICAO", () => {
    const hits = search(index, "EGLL");
    expect(hits[0]?.[0]).toBe("EGLL");
    expect(hits[0]?.[3]).toMatch(/Heathrow/i);
  });

  it("includes Heathrow by IATA", () => {
    const hits = search(index, "LHR");
    expect(hits[0]?.[0]).toBe("EGLL");
    expect(hits[0]?.[1]).toBe("LHR");
  });

  it("covers major hubs on every populated continent", () => {
    // Regression check: the index must not silently narrow to a single
    // country. Sampling one hub per continent catches the "K-prefixed
    // ICAO only" filter (and any equivalent).
    const hubs = [
      "KJFK",  // North America
      "CYYZ",  // Canada (still North America; catches K-only filter)
      "EGLL",  // Europe
      "RJTT",  // Asia (Tokyo Haneda)
      "YSSY",  // Oceania
      "FACT",  // Africa (Cape Town)
      "SBGR",  // South America (São Paulo)
    ];
    for (const icao of hubs) {
      const hit = search(index, icao)[0];
      expect(hit?.[0], `expected ${icao} in the index`).toBe(icao);
    }
  });

  it("carries an IAP flag on the 7th tuple element", () => {
    // Major hubs virtually always have a published instrument approach.
    // Guards the widened AirportIndexRow shape (see data.ts) — a
    // regression here would break cockpit's nearest-IAP reference line.
    const hit = search(index, "KSFO")[0];
    expect(hit?.[6]).toBe(1);
  });
});

describe("nearestIapAirport", () => {
  it("returns KSFO for a point just north of SFO's field", () => {
    // A point ~2 nm north of KSFO — the nearest IAP airport should be
    // KSFO (37.6198, -122.3748), not something farther away.
    const nearest = nearestIapAirport(index, 37.65, -122.375);
    expect(nearest?.icao).toBe("KSFO");
    expect(nearest?.distanceNm).toBeLessThan(3);
  });

  it("returns null when no rows have IAP", () => {
    const empty: AirportIndexRow[] = [
      ["ZZZ1", "", "Nowhere", "Nowhere", 0, 0, 0],
    ];
    expect(nearestIapAirport(empty, 37.75, -122.45)).toBeNull();
  });
});
