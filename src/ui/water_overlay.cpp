// Lake overlay — mirrors ui::land::draw exactly, just reading
// Section::Water and filling in the background color. Lakes end up as
// water-color holes carved into the land tint. Rivers are polylines
// (Section::Coast) and are drawn separately by ui::coastline::draw.
//
// Ear-clip scratch is shared across land / water / weather-map overlays via
// geo::scratch (include/geo/poly_scratch.h) — three separate 18 KB BSS pools
// added up to 54 KB, which is what pushed mbedTLS's ~52 KB per-fetch working
// set past the heap ceiling.

#include "ui/water_overlay.h"

#include "data/tile_math.h"
#include "data/tile_reader.h"
#include "data/tile_store.h"
#include "geo/ear_clip.h"
#include "geo/poly_scratch.h"
#include "services/radar_location.h"
#include "ui/layer_style.h"
#include "ui/map_projection.hpp"
#include "ui/radar_theme.h"

namespace ui::water {
namespace {

constexpr size_t kMaxPolyVerts = 1024;
using geo::scratch::earClip;
using geo::scratch::triBuf;
using geo::scratch::verts;
static_assert(kMaxPolyVerts <= geo::scratch::kMaxVerts,
              "shared scratch pool too small for this overlay");

void drawPolygon(lgfx::LGFXBase& gfx, const data::tile::PolylineView& view,
                 uint16_t color) {
  if (view.point_count < 3 || view.point_count > kMaxPolyVerts) return;
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
      proj::latLonToScreen(proj::e7ToDeg(verts[vi].y),
                            proj::e7ToDeg(verts[vi].x),
                            &x[k], &y[k]);
    }
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
  // Water = same background color as ocean — lakes read as bodies of
  // water carved back out of the land tint.
  const uint16_t color = radar::kColorBackground;
  data::tile::TileReader reader;
  if (!reader.init(bytes.data, bytes.size)) return;
  uint32_t sec_len = 0;
  const uint8_t* p = reader.sectionBegin(data::tile::Section::Water, &sec_len);
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
  // Shares the Land toggle — semantically both are the "map" layer and
  // there's no user reason to see land without lakes (or vice versa).
  if (!ui::layers::enabled(ui::layers::Layer::Land)) return;
  uint16_t tx = 0, ty = 0;
  data::tile::tileOfLatLon(data::tile::kRenderZoom,
                            services::location::lat(),
                            services::location::lon(), &tx, &ty);
  const auto bytes = data::tile::store().get(data::tile::kRenderZoom, tx, ty);
  drawFromTile(gfx, bytes);
}

}  // namespace ui::water
