// C++ mirror of the tile grid geometry in scripts/tile_scheme.py.
// The render/fetch code needs to know which (z, x, y) tile contains
// a given lat/lon; this is the on-device equivalent of tile_of().
//
// Same equirectangular scheme: 2^z tiles per side at zoom z, tile
// (0, 0) covering the north-west corner (TMS/slippy convention).
#pragma once

#include <cstdint>

namespace data::tile {

// Zoom for the device's normal render loop. z=7 tiles are ~310 km on
// a side at the equator — comfortably wider than the 25 nm range
// preset (~92 km diameter), so a single tile fills the widest
// viewport even near tile boundaries.
constexpr uint8_t kRenderZoom = 7;

// Given a zoom level, return 2^z (tile count per side).
uint32_t tilesPerSide(uint8_t z);

// Convert a lat/lon (in degrees) to the (x, y) tile it falls in at
// zoom `z`. Latitude is clamped just inside the poles so exact ±90
// don't off-by-one. Longitude wraps.
void tileOfLatLon(uint8_t z, double lat, double lon, uint16_t* x, uint16_t* y);

}  // namespace data::tile
