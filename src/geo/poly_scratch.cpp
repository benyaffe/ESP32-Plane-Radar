#include "geo/poly_scratch.h"

namespace geo::scratch {

Vertex verts[kMaxVerts];
uint16_t earClip[2 * kMaxVerts];
uint16_t triBuf[3 * (kMaxVerts - 2)];

}  // namespace geo::scratch
