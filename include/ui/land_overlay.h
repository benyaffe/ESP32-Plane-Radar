#pragma once

#include <LovyanGFX.hpp>

namespace ui::land {

/** Fill baked land triangles in a subtle tint so land vs water reads at
 *  a glance. Draw AFTER background/mask and BEFORE rings/coastline so
 *  those show on top. Triangles may overflow the outer ring — caller
 *  is responsible for masking the ring's exterior afterwards. */
void draw(lgfx::LGFXBase& gfx);

}  // namespace ui::land
