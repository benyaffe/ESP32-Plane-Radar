// Ear-clipping triangulator for simple polygons.
//
// Land + water polygons in the tile format are shipped as open outlines
// (SECTION_LAND, SECTION_WATER). The renderer fills them, which needs
// triangles. Ear-clip runs once per polygon at tile-load time — the
// output triangle list is cached alongside the vertex list.
//
// Contract:
//   * Input polygon is simple (no self-intersection) and has no holes.
//     Natural Earth land polygons after DP-simplify to 0.002° meet this.
//   * Vertex order is either CW or CCW — the algorithm normalizes to CCW
//     internally so callers don't need to care.
//   * Zero heap allocation: caller provides a scratch buffer sized to
//     the polygon's vertex count.
//
// int64 intermediates: at zoom 3 a tile spans 45°, so vertex coordinate
// differences reach ~4.5e8 (e7 microdegrees). Cross-product tests
// multiply two such differences, producing values up to ~2e17 — well
// past int32.
#pragma once

#include <cstddef>
#include <cstdint>

namespace geo {

struct Vertex {
  int32_t x;  // typically lon_e7 (packed as ident-format microdegrees)
  int32_t y;  // typically lat_e7
};

// Sentinel returned by triangulate() on failure. Not a valid triangle count.
constexpr int kEarClipError = -1;

// Triangulate a simple polygon by ear-clipping. Returns the number of
// triangles emitted (== count - 2 on success) or kEarClipError on
// failure.
//
// Parameters:
//   verts       — polygon vertices in either CW or CCW order.
//   count       — number of vertices. Must be >= 3.
//   out_tris    — output buffer. Each triangle occupies 3 consecutive
//                 uint16_t slots holding indices into `verts`. So the
//                 buffer must have room for at least 3 * (count - 2)
//                 uint16_t.
//   out_cap     — number of uint16_t slots in out_tris.
//   scratch     — 2 * count uint16_t of scratch storage for the
//                 internal doubly-linked vertex list.
//
// Failure modes:
//   * count < 3 or out_cap < 3*(count-2): returns kEarClipError
//   * Cannot find an ear (self-intersecting or otherwise pathological
//     polygon): returns kEarClipError; already-emitted triangles are
//     kept in out_tris and the returned count reflects only the failure.
int triangulate(const Vertex* verts, uint16_t count,
                uint16_t* out_tris, size_t out_cap,
                uint16_t* scratch);

}  // namespace geo
