#include "geo/ear_clip.h"

namespace geo {

namespace {

// Signed area of the polygon (2×). Positive → CCW in a standard
// math-orientation coordinate system where y grows up.
//
// Accumulator safety: each shoelace term (a.x * b.y) can be up to
// (max_coord)² in magnitude. For tile-bounded polygons where
// coordinates are e7 microdegrees within a single tile, max_coord is
// ~4.5e8 (z=3 tile spans 45°), so each term is ~2e17 and the sum
// stays well inside int64 range (~9.2e18) even for ~500-vertex
// polygons.
int64_t signedArea2(const Vertex* verts, uint16_t count) {
  int64_t s = 0;
  for (uint16_t i = 0; i < count; ++i) {
    const Vertex& a = verts[i];
    const Vertex& b = verts[(i + 1) % count];
    s += static_cast<int64_t>(a.x) * b.y - static_cast<int64_t>(b.x) * a.y;
  }
  return s;
}

// 2× signed area of triangle (a, b, c). Positive → CCW.
int64_t cross(const Vertex& a, const Vertex& b, const Vertex& c) {
  return static_cast<int64_t>(b.x - a.x) * (c.y - a.y)
       - static_cast<int64_t>(b.y - a.y) * (c.x - a.x);
}

bool triangleContains(const Vertex& a, const Vertex& b, const Vertex& c,
                      const Vertex& p) {
  const int64_t d1 = cross(a, b, p);
  const int64_t d2 = cross(b, c, p);
  const int64_t d3 = cross(c, a, p);
  const bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
  const bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
  return !(has_neg && has_pos);
}

}  // namespace

int triangulate(const Vertex* verts, uint16_t count,
                uint16_t* out_tris, size_t out_cap,
                uint16_t* scratch) {
  if (count < 3) return kEarClipError;
  const size_t needed = 3u * (count - 2);
  if (out_cap < needed) return kEarClipError;

  // scratch[0..count) = next[i], scratch[count..2*count) = prev[i]
  uint16_t* next = scratch;
  uint16_t* prev = scratch + count;

  // Force CCW orientation. If the polygon is CW, walk it in reverse
  // by swapping the direction of next/prev links — no need to touch
  // the vertex array (which is const to us).
  const bool ccw = signedArea2(verts, count) > 0;
  for (uint16_t i = 0; i < count; ++i) {
    if (ccw) {
      next[i] = static_cast<uint16_t>((i + 1) % count);
      prev[i] = static_cast<uint16_t>((i + count - 1) % count);
    } else {
      next[i] = static_cast<uint16_t>((i + count - 1) % count);
      prev[i] = static_cast<uint16_t>((i + 1) % count);
    }
  }

  uint16_t remaining = count;
  uint16_t cursor = 0;
  size_t written = 0;

  // Guard against runaway loops on pathological input: at most one full
  // sweep per triangle should succeed.
  uint16_t sweeps_without_progress = 0;

  while (remaining > 3) {
    const uint16_t i_prev = prev[cursor];
    const uint16_t i_curr = cursor;
    const uint16_t i_next = next[cursor];
    const Vertex& a = verts[i_prev];
    const Vertex& b = verts[i_curr];
    const Vertex& c = verts[i_next];

    bool is_ear = cross(a, b, c) > 0;  // convex in CCW orientation
    if (is_ear) {
      // Ensure no other polygon vertex lies inside triangle (a, b, c).
      uint16_t j = next[i_next];
      while (j != i_prev) {
        if (triangleContains(a, b, c, verts[j])) {
          is_ear = false;
          break;
        }
        j = next[j];
      }
    }

    if (is_ear) {
      if (written + 3 > out_cap) return kEarClipError;  // shouldn't happen
      out_tris[written + 0] = i_prev;
      out_tris[written + 1] = i_curr;
      out_tris[written + 2] = i_next;
      written += 3;
      next[i_prev] = i_next;
      prev[i_next] = i_prev;
      --remaining;
      cursor = i_next;
      sweeps_without_progress = 0;
    } else {
      cursor = next[cursor];
      ++sweeps_without_progress;
      if (sweeps_without_progress > count) {
        // Made a full loop without finding an ear — polygon is
        // pathological (self-intersecting, colinear, etc). Return
        // what we have so callers get *something* to render.
        return kEarClipError;
      }
    }
  }

  // Final triangle: three remaining verts.
  if (remaining == 3) {
    if (written + 3 > out_cap) return kEarClipError;
    const uint16_t i_prev = prev[cursor];
    const uint16_t i_next = next[cursor];
    out_tris[written + 0] = i_prev;
    out_tris[written + 1] = cursor;
    out_tris[written + 2] = i_next;
    written += 3;
  }

  return static_cast<int>(written / 3);
}

}  // namespace geo
