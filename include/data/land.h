#pragma once

#include <cstddef>
#include <cstdint>

// Baked landmass triangles (Natural Earth 1:10m land + minor islands)
// clipped and triangulated around the radar center.
// Regenerate with scripts/build_land.py.

namespace data::land {

struct Vertex {
  int32_t lat_e7;
  int32_t lon_e7;
};

struct Triangle {
  uint16_t v0;
  uint16_t v1;
  uint16_t v2;
};

extern const Vertex kVertices[];
extern const Triangle kTriangles[];
extern const size_t kVertexCount;
extern const size_t kTriangleCount;

}  // namespace data::land
