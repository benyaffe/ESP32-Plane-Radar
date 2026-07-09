// Colors and geometric constants for the radar view. Mirrored from
// firmware include/ui/radar_theme.h so the web visuals track hardware.

export const SIZE = 240;
export const CENTER_X = 120;
export const CENTER_Y = 120;
export const GRID_OUTER_RADIUS = 107;
export const PHYSICAL_PANEL_RADIUS = 120;

export const RANGE_PRESETS = [
  { nm: 5, outerKm: 9.26 },
  { nm: 10, outerKm: 18.52 },
  { nm: 15, outerKm: 27.78 },
  { nm: 25, outerKm: 46.3 },
];

export const KM_PER_DEG = 111.0;

// RGB CSS colors — the browser handles color directly, so we skip the
// 565 conversion path the firmware uses on GC9A01.
export const COLORS = {
  background:      "rgb(4, 10, 28)",
  grid:            "rgb(16, 101, 33)",
  label:           "rgb(255, 255, 255)",
  aircraft:        "rgb(60, 130, 255)",
  trackVector:     "rgb(230, 230, 230)",
  tagType:         "rgb(80, 190, 255)",
  tagAltitude:     "rgb(255, 240, 100)",
  runway:          "rgb(70, 170, 200)",
  runwayLabel:     "rgb(110, 210, 240)",
  land:            "rgb(12, 20, 36)",       // dim land tint
  coastline:       "rgb(44, 70, 68)",       // slightly brighter than land
  road:            "rgb(110, 110, 130)",
  emergency:       "rgb(255, 0, 0)",
  centerDot:       "rgb(255, 255, 255)",
} as const;

// Weather-view category colors — match src/ui/weather_map.cpp.
export const WX_COLORS = {
  VFR:  "rgb(40, 200, 60)",
  MVFR: "rgb(70, 130, 255)",
  IFR:  "rgb(240, 70, 70)",
  LIFR: "rgb(220, 70, 200)",
  Unknown: "rgb(120, 120, 120)",
} as const;
