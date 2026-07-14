#include "ui/land_overlay.h"

#include "data/tile_math.h"
#include "data/tile_reader.h"
#include "data/tile_store.h"
#include "geo/ear_clip.h"
#include "geo/poly_scratch.h"
#include "services/radar_location.h"
#include "ui/layer_style.h"
#include "ui/map_projection.hpp"
#include "ui/radar_theme.h"

namespace ui::land {
namespace {

// Per-polygon scratch used to ear-clip a tile land polygon into
// triangles on each frame. Statically allocated (single-threaded
// Arduino loop, no contention). Sizes chosen to cover the biggest
// realistic polygon in a z=7 tile at 0.002° simplification:
//   * 1024 vertices is comfortable headroom over the median
//     ~30-100 verts and the ~500-vert worst case seen in continental
//     coastline polygons at this zoom.
constexpr size_t kMaxPolyVerts = 1024;
// Ear-clip scratch is now shared via geo::scratch (include/geo/poly_scratch.h);
// three overlays used to duplicate 18 KB of BSS each — consolidating frees
// ~36 KB of heap ceiling for mbedTLS's ~52 KB per-fetch working set.
using geo::scratch::earClip;
using geo::scratch::triBuf;
using geo::scratch::verts;
static_assert(kMaxPolyVerts <= geo::scratch::kMaxVerts,
              "shared scratch pool too small for this overlay");

// Draw one tile land polygon by ear-clipping and filling the
// resulting triangles.
void drawPolygon(lgfx::LGFXBase& gfx, const data::tile::PolylineView& view,
                 uint16_t color) {
  if (view.point_count < 3 || view.point_count > kMaxPolyVerts) return;
  // Load the polygon into ear-clip vertex layout — x=lon_e7, y=lat_e7.
  for (uint16_t i = 0; i < view.point_count; ++i) {
    int32_t lat_e7 = 0, lon_e7 = 0;
    view.getPoint(i, &lat_e7, &lon_e7);
    verts[i].x = lon_e7;
    verts[i].y = lat_e7;
  }
  const int tri_count = geo::triangulate(
      verts, view.point_count, triBuf, sizeof(triBuf) / sizeof(triBuf[0]),
      earClip);
  if (tri_count <= 0) return;
  for (int t = 0; t < tri_count; ++t) {
    int x[3];
    int y[3];
    for (int k = 0; k < 3; ++k) {
      const uint16_t vi = triBuf[t * 3 + k];
      // ear_clip stored lat_e7 as .y, lon_e7 as .x — recover for the
      // equirectangular projection.
      proj::latLonToScreen(proj::e7ToDeg(verts[vi].y),
                            proj::e7ToDeg(verts[vi].x),
                            &x[k], &y[k]);
    }
    // Quick reject same as the baked path — all 3 verts off the same
    // side of the 240×240 canvas.
    if ((x[0] < 0 && x[1] < 0 && x[2] < 0) ||
        (x[0] > 239 && x[1] > 239 && x[2] > 239) ||
        (y[0] < 0 && y[1] < 0 && y[2] < 0) ||
        (y[0] > 239 && y[1] > 239 && y[2] > 239)) {
      continue;
    }
    gfx.fillTriangle(x[0], y[0], x[1], y[1], x[2], y[2], color);
  }
}

void drawFromTile(lgfx::LGFXBase& gfx, const data::tile::TileBytes& bytes) {
  const uint16_t color = radar::kColorLand;
  data::tile::TileReader reader;
  if (!reader.init(bytes.data, bytes.size)) return;
  uint32_t sec_len = 0;
  const uint8_t* p = reader.sectionBegin(data::tile::Section::Land, &sec_len);
  if (p == nullptr || sec_len == 0) return;
  const uint8_t* end = p + sec_len;
  uint16_t poly_count = 0;
  if (!data::tile::TileReader::readSectionCount(&p, end, &poly_count)) return;
  for (uint16_t i = 0; i < poly_count; ++i) {
    data::tile::PolylineView view;
    if (!data::tile::TileReader::readPolyline(&p, end, &view)) return;
    drawPolygon(gfx, view, color);
  }
}

}  // namespace

void draw(lgfx::LGFXBase& gfx) {
  if (!ui::layers::enabled(ui::layers::Layer::Land)) return;

  // Fallback tile carries a low-detail world land section, so this
  // path renders continents at boot even before the real z=7 tile is
  // fetched — no baked Bay Area shortcut needed.
  uint16_t tx = 0, ty = 0;
  data::tile::tileOfLatLon(data::tile::kRenderZoom,
                            services::location::lat(),
                            services::location::lon(), &tx, &ty);
  const auto bytes = data::tile::store().get(data::tile::kRenderZoom, tx, ty);
  drawFromTile(gfx, bytes);
}

}  // namespace ui::land
