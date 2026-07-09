#pragma once

// Weather-map alternate view. Triple-tap BOOT enters this mode; another
// tap of any kind exits back to the radar view. Renders a compact Bay
// Area map showing each airport as a dot colored by its current flight
// category (VFR/MVFR/IFR/LIFR).

namespace ui::weather {

/** Kick a background METAR fetch if the cache is stale. Non-blocking on
 *  native (uses main-loop polling); the render will show whatever data
 *  is cached until the next fetch lands. */
void refresh();

/** Draw one weather-map frame to the panel. Called from the main loop
 *  while weather-view mode is active. */
void draw();

}  // namespace ui::weather
