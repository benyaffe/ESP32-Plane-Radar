import { describe, expect, it } from "vitest";
import {
  bearingDeg,
  compass8,
  distSqFromCenter,
  makeView,
  project,
  segmentOnScreen,
} from "./projection";
import { CENTER_X, CENTER_Y, GRID_OUTER_RADIUS } from "./theme";

const SF_LAT = 37.7552;
const SF_LON = -122.4528;

describe("makeView", () => {
  it("derives pxPerKm from GRID_OUTER_RADIUS / outerKm", () => {
    const v = makeView(SF_LAT, SF_LON, 46.3);
    expect(v.pxPerKm).toBeCloseTo(GRID_OUTER_RADIUS / 46.3, 6);
    expect(v.centerLat).toBe(SF_LAT);
    expect(v.centerLon).toBe(SF_LON);
    expect(v.outerKm).toBe(46.3);
  });
});

describe("project", () => {
  it("projects the center itself to (CENTER_X, CENTER_Y)", () => {
    const v = makeView(SF_LAT, SF_LON, 46.3);
    const [x, y] = project(v, SF_LAT, SF_LON);
    expect(x).toBe(CENTER_X);
    expect(y).toBe(CENTER_Y);
  });

  it("puts a point due north above the center (smaller y)", () => {
    const v = makeView(SF_LAT, SF_LON, 46.3);
    const [x, y] = project(v, SF_LAT + 0.1, SF_LON);
    expect(x).toBe(CENTER_X);
    expect(y).toBeLessThan(CENTER_Y);
  });

  it("puts a point due east to the right of center (larger x)", () => {
    const v = makeView(SF_LAT, SF_LON, 46.3);
    const [x, y] = project(v, SF_LAT, SF_LON + 0.1);
    expect(x).toBeGreaterThan(CENTER_X);
    expect(y).toBe(CENTER_Y);
  });

  it("applies cos(centerLat) longitude compression near the equator vs poles", () => {
    const equator = makeView(0, 0, 100);
    const highLat = makeView(60, 0, 100);
    const [xEq] = project(equator, 0, 1);
    const [xHi] = project(highLat, 60, 1);
    // 1° of longitude at 60°N is ~half as many km as at the equator, so
    // it should project to ~half the horizontal pixel offset.
    const dxEq = xEq - CENTER_X;
    const dxHi = xHi - CENTER_X;
    expect(dxHi).toBeLessThan(dxEq * 0.6);
    expect(dxHi).toBeGreaterThan(dxEq * 0.4);
  });
});

describe("distSqFromCenter", () => {
  it("is zero at the center", () => {
    expect(distSqFromCenter(CENTER_X, CENTER_Y)).toBe(0);
  });

  it("is (r)^2 at distance r along an axis", () => {
    expect(distSqFromCenter(CENTER_X + 10, CENTER_Y)).toBe(100);
    expect(distSqFromCenter(CENTER_X, CENTER_Y - 10)).toBe(100);
  });

  it("agrees with pythagoras", () => {
    expect(distSqFromCenter(CENTER_X + 3, CENTER_Y + 4)).toBe(25);
  });
});

describe("segmentOnScreen", () => {
  it("is true for a segment fully inside", () => {
    expect(segmentOnScreen(50, 50, 150, 150)).toBe(true);
  });

  it("is false for a segment fully off the left edge", () => {
    expect(segmentOnScreen(-30, 50, -10, 100)).toBe(false);
  });

  it("is false for a segment fully off the bottom edge", () => {
    expect(segmentOnScreen(50, 500, 100, 600)).toBe(false);
  });

  it("is true for a segment that straddles the edge", () => {
    expect(segmentOnScreen(-30, 100, 100, 100)).toBe(true);
  });
});

describe("bearingDeg", () => {
  it("returns ~0 for a point due north", () => {
    expect(bearingDeg(SF_LAT, SF_LON, SF_LAT + 1, SF_LON)).toBeCloseTo(0, 1);
  });

  it("returns ~90 for a point due east", () => {
    expect(bearingDeg(SF_LAT, SF_LON, SF_LAT, SF_LON + 1)).toBeCloseTo(90, 0);
  });

  it("returns ~180 for a point due south", () => {
    expect(bearingDeg(SF_LAT, SF_LON, SF_LAT - 1, SF_LON)).toBeCloseTo(180, 1);
  });

  it("returns ~270 for a point due west", () => {
    expect(bearingDeg(SF_LAT, SF_LON, SF_LAT, SF_LON - 1)).toBeCloseTo(270, 0);
  });

  it("returns a value in [0, 360) for arbitrary inputs", () => {
    const b = bearingDeg(SF_LAT, SF_LON, 40.7128, -74.006);
    expect(b).toBeGreaterThanOrEqual(0);
    expect(b).toBeLessThan(360);
  });
});

describe("compass8", () => {
  it("bins the 8 cardinal / intercardinal centers", () => {
    expect(compass8(0)).toBe("N");
    expect(compass8(45)).toBe("NE");
    expect(compass8(90)).toBe("E");
    expect(compass8(135)).toBe("SE");
    expect(compass8(180)).toBe("S");
    expect(compass8(225)).toBe("SW");
    expect(compass8(270)).toBe("W");
    expect(compass8(315)).toBe("NW");
  });

  it("rounds to the nearest bin", () => {
    expect(compass8(22)).toBe("N");     // 22 rounds down to 0
    expect(compass8(23)).toBe("NE");    // 23 rounds up to 45
    expect(compass8(359)).toBe("N");
  });

  it("normalizes negative and wrap-around inputs", () => {
    expect(compass8(-45)).toBe("NW");
    expect(compass8(720 + 90)).toBe("E");
  });
});
