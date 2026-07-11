#pragma once

#include <LovyanGFX.hpp>

namespace ui::water {

/** Fill baked lake polygons in the background color so they read as
 *  water carved into land. Draw AFTER ui::land::draw and BEFORE
 *  ui::coastline::draw so lakes replace their land tile and rivers /
 *  coastlines still overlay everything. Same data source and toggle
 *  as land — Section::Water in the tile pyramid, fed by
 *  ne_10m_lakes in scripts/build_tiles.py. */
void draw(lgfx::LGFXBase& gfx);

}  // namespace ui::water
