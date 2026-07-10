import { describe, expect, it } from "vitest";
import {
  ceilingFromClouds,
  deriveCategory,
  distanceNm,
} from "./weather";

describe("deriveCategory (FAA flight rules)", () => {
  it("returns VFR when no ceiling and >5 sm visibility", () => {
    expect(deriveCategory(Infinity, 10)).toBe("VFR");
  });

  it("returns MVFR at ceiling 3000 ft (upper MVFR boundary)", () => {
    expect(deriveCategory(3000, 10)).toBe("MVFR");
  });

  it("returns VFR at ceiling 3001 ft (just above MVFR)", () => {
    expect(deriveCategory(3001, 10)).toBe("VFR");
  });

  it("returns IFR at ceiling 999 ft (just below IFR/MVFR boundary)", () => {
    expect(deriveCategory(999, 10)).toBe("IFR");
  });

  it("returns LIFR at ceiling 499 ft (just below IFR/LIFR boundary)", () => {
    expect(deriveCategory(499, 10)).toBe("LIFR");
  });

  it("returns MVFR at visibility 5 sm with high ceiling", () => {
    expect(deriveCategory(Infinity, 5)).toBe("MVFR");
  });

  it("returns IFR at visibility <3 sm with high ceiling", () => {
    expect(deriveCategory(Infinity, 2)).toBe("IFR");
  });

  it("returns LIFR at visibility <1 sm with high ceiling", () => {
    expect(deriveCategory(Infinity, 0)).toBe("LIFR");
  });

  it("picks the WORST of ceiling and visibility categories", () => {
    // Ceiling VFR (5000), vis LIFR (0.5) → LIFR wins.
    expect(deriveCategory(5000, 0)).toBe("LIFR");
    // Ceiling IFR (800), vis VFR (10) → IFR wins.
    expect(deriveCategory(800, 10)).toBe("IFR");
  });
});

describe("ceilingFromClouds", () => {
  it("returns Infinity when clouds is null or empty", () => {
    expect(ceilingFromClouds(null)).toBe(Infinity);
    expect(ceilingFromClouds(undefined)).toBe(Infinity);
    expect(ceilingFromClouds([])).toBe(Infinity);
  });

  it("ignores FEW and SCT layers (only BKN/OVC/VV count as ceiling)", () => {
    // 300m base = ~984 ft, but FEW/SCT don't count → no ceiling.
    const clouds = [
      { base: 300, amount: "FEW" },
      { base: 500, amount: "SCT" },
    ];
    expect(ceilingFromClouds(clouds)).toBe(Infinity);
  });

  it("returns the lowest BKN layer in feet", () => {
    // 305 m ≈ 1000 ft; 610 m ≈ 2000 ft; lowest wins.
    const clouds = [
      { base: 610, amount: "BKN" },
      { base: 305, amount: "BKN" },
    ];
    expect(ceilingFromClouds(clouds)).toBeCloseTo(1000, -1);
  });

  it("counts OVC as a ceiling", () => {
    const clouds = [{ base: 152.4, amount: "OVC" }]; // ~500 ft
    expect(ceilingFromClouds(clouds)).toBeCloseTo(500, 0);
  });

  it("counts VV (vertical visibility) as a ceiling", () => {
    const clouds = [{ base: 30.48, amount: "VV" }]; // ~100 ft
    expect(ceilingFromClouds(clouds)).toBeCloseTo(100, 0);
  });

  it("is case-insensitive on layer type", () => {
    const clouds = [{ base: 305, amount: "bkn" }];
    expect(ceilingFromClouds(clouds)).toBeCloseTo(1000, -1);
  });
});

describe("distanceNm", () => {
  it("is zero for the same point", () => {
    expect(distanceNm(37.7, -122.4, 37.7, -122.4)).toBe(0);
  });

  it("returns ~60 nm for 1° of latitude", () => {
    // Same longitude, 1° apart in latitude.
    expect(distanceNm(37.0, -122.0, 38.0, -122.0)).toBeCloseTo(60, 1);
  });

  it("scales longitude by cos(lat)", () => {
    // 1° of longitude at the equator ≈ 60 nm; at 60°N ≈ 30 nm.
    const equatorDist = distanceNm(0, 0, 0, 1);
    const highLatDist = distanceNm(60, 0, 60, 1);
    expect(equatorDist).toBeCloseTo(60, 1);
    expect(highLatDist).toBeCloseTo(30, 1);
  });
});
