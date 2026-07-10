import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { decodeTile, TileSection } from "./tile";

// Round-trip check: decode a real z=7 SF-area tile (the one the
// emulator loads at boot) and confirm the decoder pulls the same
// (z, x, y) it was baked with, plus a plausible set of sections.
// If the Python encoder ever drifts, this test breaks BEFORE we ship
// a mismatched web build.
const SF_TILE_PATH = resolve(
  __dirname,
  "..", "..", "data", "emulator_bootstrap_tile_7_20_37.bin",
);

function loadSfTile(): ArrayBuffer {
  const buf = readFileSync(SF_TILE_PATH);
  return buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength);
}

describe("decodeTile", () => {
  it("reads the (z, x, y) baked into the SF tile header", () => {
    const t = decodeTile(loadSfTile());
    expect(t.z).toBe(7);
    expect(t.x).toBe(20);
    expect(t.y).toBe(37);
  });

  it("returns polylines with in-range lon/lat pairs", () => {
    const t = decodeTile(loadSfTile());
    // z=7 tile (20, 37) covers the SF Bay Area at these approximate
    // lat/lon bounds. Any point in coast/land should land inside.
    for (const poly of [...t.coast, ...t.land]) {
      for (const [lon, lat] of poly) {
        expect(lat).toBeGreaterThan(30);
        expect(lat).toBeLessThan(45);
        expect(lon).toBeGreaterThan(-130);
        expect(lon).toBeLessThan(-115);
      }
    }
  });

  it("decodes airports with 8-char idents trimmed of NUL padding", () => {
    const t = decodeTile(loadSfTile());
    expect(t.airports.length).toBeGreaterThan(0);
    for (const a of t.airports) {
      expect(a.ident.length).toBeGreaterThan(0);
      expect(a.ident.length).toBeLessThanOrEqual(8);
      // ASCII, no NULs
      for (let i = 0; i < a.ident.length; i++) {
        const c = a.ident.charCodeAt(i);
        expect(c).toBeGreaterThanOrEqual(0x20);
        expect(c).toBeLessThan(0x80);
      }
      expect(a.tier).toBeGreaterThanOrEqual(0);
      expect(a.tier).toBeLessThanOrEqual(3);
      expect(a.lat).toBeGreaterThan(30);
      expect(a.lat).toBeLessThan(45);
      expect(a.lon).toBeGreaterThan(-130);
      expect(a.lon).toBeLessThan(-115);
    }
  });

  it("finds KSFO in the SF tile with the correct coords", () => {
    const t = decodeTile(loadSfTile());
    const sfo = t.airports.find(a => a.ident === "KSFO");
    expect(sfo).toBeDefined();
    // KSFO reference: 37.6188 / -122.3750
    expect(sfo!.lat).toBeCloseTo(37.62, 1);
    expect(sfo!.lon).toBeCloseTo(-122.38, 1);
  });

  it("rejects a buffer smaller than the header", () => {
    expect(() => decodeTile(new ArrayBuffer(4))).toThrow(/smaller than header/);
  });

  it("rejects a buffer with the wrong magic", () => {
    const bad = new Uint8Array(16);
    bad.set([0x58, 0x59, 0x5A, 0x5A]);
    expect(() => decodeTile(bad.buffer)).toThrow(/bad magic/);
  });

  it("exposes TileSection ids that match tile_format.py", () => {
    // Guard against renumbering — the Python encoder and JS decoder
    // must agree on these constants.
    expect(TileSection.Coast).toBe(0);
    expect(TileSection.Land).toBe(1);
    expect(TileSection.Water).toBe(2);
    expect(TileSection.Airports).toBe(3);
  });
});
