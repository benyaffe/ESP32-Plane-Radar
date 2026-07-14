#pragma once

#include <LovyanGFX.hpp>

namespace ui {

/** Draw the static sonar/radar grid (black disc, green overlay, labels). */
void radarDisplayDraw();

/** Redraw aircraft only (blits cached grid; no full-screen clear). */
void radarDisplayRefreshAircraft();

/** Shared off-screen 240×240 16-bit sprite. Both the radar view and the
 *  weather view composite into this and pushSprite in one blit to avoid
 *  the tearing/flash you get when drawing full-screen straight to the
 *  panel every ~1 s. Returns nullptr if the sprite failed to allocate
 *  (fall back to direct-to-panel draw in that case). */
LGFX_Sprite* radarDisplayFrameSprite();

/** Force-allocate the off-screen frame sprite now, so subsequent renders
 *  don't race with heap fragmentation from tile fetches / TLS record
 *  buffers. Call at boot before any Wi-Fi/tile work. Returns false if
 *  the alloc failed; direct-to-panel fallback still works but flickers. */
bool radarDisplayPreallocateFrameSprite();

}  // namespace ui
