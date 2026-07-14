// Shared ear-clip scratch pool for the land / water / weather-map
// overlays. All three used to declare their own file-static copy
// (18 KB each = 54 KB BSS). Rendering is single-threaded on the
// Arduino loop, so they trivially share one pool — freeing ~36 KB
// of RAM ceiling that mbedTLS desperately needs during HTTPS fetches.
//
// Sized to cover the biggest realistic polygon in a z=7 tile at
// 0.002° simplification: 1024 vertices is comfortable headroom over
// the median ~30-100 verts and the ~500-vert worst case seen in
// continental land masses.
#pragma once

#include <cstddef>
#include <cstdint>

#include "geo/ear_clip.h"

namespace geo::scratch {

constexpr size_t kMaxVerts = 1024;

extern Vertex verts[kMaxVerts];
extern uint16_t earClip[2 * kMaxVerts];
extern uint16_t triBuf[3 * (kMaxVerts - 2)];
constexpr size_t kMaxTriIndices = 3 * (kMaxVerts - 2);

}  // namespace geo::scratch
