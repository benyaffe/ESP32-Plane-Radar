// Binary tile decoder. Matches scripts/tile_format.py byte-for-byte:
// same magic, same section IDs, same little-endian polyline / airport
// layout. One entry point (decodeTile) hands back polylines-as-lonlat
// so the renderer doesn't care about the wire format.
//
// Format spec lives in scripts/tile_format.py's module docstring; the
// short version is a 12-byte header, then a section index (12 bytes
// per entry), then each section's payload. Points are int32
// microdegrees (lat * 1e7, lon * 1e7). See that file before editing.

export type LonLat = [number, number];  // [lon, lat] to match the rest of web/

export enum TileSection {
  Coast = 0,
  Land = 1,
  Water = 2,
  Airports = 3,
}

export interface TileRunway {
  lat1: number; lon1: number;
  lat2: number; lon2: number;
}

export interface TileAirport {
  ident: string;
  lat: number;
  lon: number;
  tier: number;                 // 0=none, 1=small, 2=medium, 3=large
  instrumentApproach: boolean;
  runways: TileRunway[];
}

export interface Tile {
  z: number;
  x: number;
  y: number;
  coast: LonLat[][];            // one polyline per array
  land: LonLat[][];             // outer ring per polygon (holes not encoded)
  water: LonLat[][];
  airports: TileAirport[];
}

const MAGIC = 0x31545250;       // "PRT1" little-endian
const VERSION = 1;
const HEADER_SIZE = 12;
const INDEX_ENTRY_SIZE = 12;
const E7 = 1e-7;

function decodePolylines(view: DataView, off: number, len: number): LonLat[][] {
  const end = off + len;
  const polys: LonLat[][] = [];
  const polyCount = view.getUint16(off, true);
  off += 2;
  for (let i = 0; i < polyCount; i++) {
    if (off + 2 > end) throw new Error("tile: truncated polyline count");
    const ptCount = view.getUint16(off, true);
    off += 2;
    // Skip bbox — the browser re-derives from the actual points and
    // there's no wire-format benefit to trusting a possibly-drifted
    // baked bbox.
    off += 16;
    if (off + ptCount * 8 > end) throw new Error("tile: truncated polyline points");
    const pts: LonLat[] = new Array(ptCount);
    for (let k = 0; k < ptCount; k++) {
      const latE7 = view.getInt32(off, true);
      const lonE7 = view.getInt32(off + 4, true);
      off += 8;
      pts[k] = [lonE7 * E7, latE7 * E7];
    }
    polys.push(pts);
  }
  return polys;
}

function decodeAirports(view: DataView, off: number, len: number): TileAirport[] {
  const end = off + len;
  const airports: TileAirport[] = [];
  const count = view.getUint16(off, true);
  off += 2;
  const identBytes = new Uint8Array(8);
  const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
  for (let i = 0; i < count; i++) {
    if (off + 18 > end) throw new Error("tile: truncated airport header");
    const latE7 = view.getInt32(off, true);
    const lonE7 = view.getInt32(off + 4, true);
    const flags = view.getUint8(off + 8);
    identBytes.set(bytes.subarray(off + 9, off + 17));
    const runwayCount = view.getUint8(off + 17);
    off += 18;
    // Trim trailing NULs from the 8-byte ASCII ident field.
    let identLen = 8;
    while (identLen > 0 && identBytes[identLen - 1] === 0) identLen--;
    const ident = identLen > 0
        ? String.fromCharCode(...identBytes.subarray(0, identLen))
        : "";
    if (off + runwayCount * 16 > end) throw new Error("tile: truncated runways");
    const runways: TileRunway[] = new Array(runwayCount);
    for (let r = 0; r < runwayCount; r++) {
      runways[r] = {
        lat1: view.getInt32(off, true) * E7,
        lon1: view.getInt32(off + 4, true) * E7,
        lat2: view.getInt32(off + 8, true) * E7,
        lon2: view.getInt32(off + 12, true) * E7,
      };
      off += 16;
    }
    airports.push({
      ident,
      lat: latE7 * E7,
      lon: lonE7 * E7,
      tier: flags & 0b11,
      instrumentApproach: (flags & 0b100) !== 0,
      runways,
    });
  }
  return airports;
}

export function decodeTile(buffer: ArrayBuffer): Tile {
  if (buffer.byteLength < HEADER_SIZE) {
    throw new Error("tile: file smaller than header");
  }
  const view = new DataView(buffer);
  const magic = view.getUint32(0, true);
  if (magic !== MAGIC) throw new Error("tile: bad magic");
  const version = view.getUint8(4);
  if (version !== VERSION) throw new Error(`tile: unsupported version ${version}`);
  const z = view.getUint8(5);
  const x = view.getUint16(6, true);
  const y = view.getUint16(8, true);
  const sectionCount = view.getUint8(10);
  // byte 11 is reserved

  const tile: Tile = { z, x, y, coast: [], land: [], water: [], airports: [] };
  const indexOff = HEADER_SIZE;
  if (indexOff + sectionCount * INDEX_ENTRY_SIZE > buffer.byteLength) {
    throw new Error("tile: index runs past end of file");
  }
  for (let i = 0; i < sectionCount; i++) {
    const entryOff = indexOff + i * INDEX_ENTRY_SIZE;
    const kind = view.getUint8(entryOff);
    const payloadOff = view.getUint32(entryOff + 4, true);
    const payloadLen = view.getUint32(entryOff + 8, true);
    if (payloadOff + payloadLen > buffer.byteLength) {
      throw new Error(`tile: section ${kind} runs past end of file`);
    }
    switch (kind) {
      case TileSection.Coast:
        tile.coast = decodePolylines(view, payloadOff, payloadLen);
        break;
      case TileSection.Land:
        tile.land = decodePolylines(view, payloadOff, payloadLen);
        break;
      case TileSection.Water:
        tile.water = decodePolylines(view, payloadOff, payloadLen);
        break;
      case TileSection.Airports:
        tile.airports = decodeAirports(view, payloadOff, payloadLen);
        break;
      default:
        // Unknown section — skip so newer bakes with extra sections still
        // decode on older clients.
        break;
    }
  }
  return tile;
}
