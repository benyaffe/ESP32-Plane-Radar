#include "ui/coastline_overlay.h"

#include <cstdio>

#include "data/tile_math.h"
#include "data/tile_reader.h"
#include "data/tile_store.h"
#include "services/radar_location.h"
#include "ui/layer_style.h"
#include "ui/map_projection.hpp"
#include "ui/radar_theme.h"

namespace ui::coastline {
namespace {

// Coastline is grid-family clutter — dim green, thin, sits below labels
// and aircraft. Uses the same palette entry as the grid rings for now
// (M4/M6 will move this to per-layer style + clarity gating).
inline uint16_t coastColor() { return radar::kColorGrid; }

// Draw coastlines from a fetched tile — iterate the SECTION_COAST
// polylines through the TileReader API.
void drawFromTile(lgfx::LGFXBase& gfx, const data::tile::TileBytes& bytes) {
  const uint16_t color = coastColor();
  data::tile::TileReader reader;
  if (!reader.init(bytes.data, bytes.size)) return;
  uint32_t sec_len = 0;
  const uint8_t* p = reader.sectionBegin(data::tile::Section::Coast, &sec_len);
  if (p == nullptr || sec_len == 0) return;
  const uint8_t* end = p + sec_len;
  uint16_t poly_count = 0;
  if (!data::tile::TileReader::readSectionCount(&p, end, &poly_count)) return;
  for (uint16_t i = 0; i < poly_count; ++i) {
    data::tile::PolylineView view;
    if (!data::tile::TileReader::readPolyline(&p, end, &view)) return;
    if (view.point_count < 2) continue;

    int32_t lat_e7 = 0, lon_e7 = 0;
    view.getPoint(0, &lat_e7, &lon_e7);
    int prev_x = 0, prev_y = 0;
    proj::latLonToScreen(proj::e7ToDeg(lat_e7), proj::e7ToDeg(lon_e7),
                         &prev_x, &prev_y);
    for (uint16_t j = 1; j < view.point_count; ++j) {
      view.getPoint(j, &lat_e7, &lon_e7);
      int x = 0, y = 0;
      proj::latLonToScreen(proj::e7ToDeg(lat_e7), proj::e7ToDeg(lon_e7), &x, &y);
      int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
      if (proj::clipSegmentToDisc(prev_x, prev_y, x, y, &cx0, &cy0, &cx1, &cy1)) {
        gfx.drawLine(cx0, cy0, cx1, cy1, color);
      }
      prev_x = x;
      prev_y = y;
    }
  }
}

}  // namespace

void draw(lgfx::LGFXBase& gfx) {
  if (!ui::layers::enabled(ui::layers::Layer::Coastline)) return;

  // Pull the tile the device is currently sitting in. The fallback
  // tile has no coast section — quiet until a real tile arrives.
  uint16_t tx = 0, ty = 0;
  data::tile::tileOfLatLon(data::tile::kRenderZoom,
                            services::location::lat(),
                            services::location::lon(), &tx, &ty);
  const auto bytes = data::tile::store().get(data::tile::kRenderZoom, tx, ty);
  drawFromTile(gfx, bytes);
}

}  // namespace ui::coastline
