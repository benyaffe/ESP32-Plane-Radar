#pragma once

// Cockpit-style clock face + outside air temperature. Third screen in the
// triple-tap cycle: Radar → METAR → Cockpit → Radar.

namespace ui::cockpit {

/** Cheap idempotent — safe to call at boot. */
void init();

/** Kick any background work (weather refresh) needed to keep the display
 *  current. Non-blocking; call once per redraw. */
void refresh();

/** Composite one frame into the shared 240×240 sprite and blit. Call at
 *  ~1 Hz to keep the second-sweep smooth. */
void draw();

}  // namespace ui::cockpit
