// Global application state — center + range + view + layer toggles.
// Kept as a module-scoped store rather than a full state library
// because the preview has narrow needs.

import { RANGE_PRESETS } from "./theme";

export type LayerId = "coast" | "land" | "roads" | "runways" | "tags";
export type ViewMode = "radar" | "weather";

export interface FocusPoint {
  label: string;
  lat: number;
  lon: number;
  defaultRangeIdx: number;
  isHome: boolean;
}

// Bay Area focus ring — same shape as services::focus in the firmware.
// First entry is the default center: Sutro Tower (well-known SF broadcast
// landmark), NOT a private residence. isHome flags the "starting" point
// so the firmware knows to fall through to its persisted user location
// when actually running on hardware.
export const FOCUS_RING: FocusPoint[] = [
  { label: "Sutro", lat: 37.7552, lon: -122.4528, defaultRangeIdx: 1, isHome: true  },
  { label: "SFO",   lat: 37.6188, lon: -122.3750, defaultRangeIdx: 1, isHome: false },
  { label: "OAK",   lat: 37.7213, lon: -122.2214, defaultRangeIdx: 1, isHome: false },
  { label: "SJC",   lat: 37.3639, lon: -121.9289, defaultRangeIdx: 1, isHome: false },
  { label: "HWD",   lat: 37.6591, lon: -122.1214, defaultRangeIdx: 0, isHome: false },
  { label: "SQL",   lat: 37.5119, lon: -122.2495, defaultRangeIdx: 0, isHome: false },
  { label: "PAO",   lat: 37.4611, lon: -122.1150, defaultRangeIdx: 0, isHome: false },
  { label: "HAF",   lat: 37.5136, lon: -122.5006, defaultRangeIdx: 0, isHome: false },
];

export interface AppState {
  centerLat: number;
  centerLon: number;
  centerLabel: string;     // "Home" | "SFO" | etc.
  focusIdx: number;        // index into FOCUS_RING; -1 = custom (from typeahead)
  rangeIdx: number;        // index into RANGE_PRESETS
  view: ViewMode;
  layers: Record<LayerId, boolean>;
}

// Default: focus[0] = Home, 10 nm range, radar view. Roads default OFF —
// the user found them noisy under traffic.
export const state: AppState = {
  centerLat: FOCUS_RING[0].lat,
  centerLon: FOCUS_RING[0].lon,
  centerLabel: FOCUS_RING[0].label,
  focusIdx: 0,
  rangeIdx: FOCUS_RING[0].defaultRangeIdx,
  view: "radar",
  layers: {
    coast: true,
    land: true,
    roads: false,          // opt-in per user preference
    runways: true,
    tags: true,
  },
};

type Listener = () => void;
const listeners: Listener[] = [];

export function subscribe(fn: Listener): () => void {
  listeners.push(fn);
  return () => {
    const idx = listeners.indexOf(fn);
    if (idx >= 0) listeners.splice(idx, 1);
  };
}

export function notify(): void {
  for (const fn of listeners) fn();
}

export function cycleRange(): void {
  state.rangeIdx = (state.rangeIdx + 1) % RANGE_PRESETS.length;
  notify();
}

export function cycleFocus(): void {
  state.focusIdx = (state.focusIdx + 1) % FOCUS_RING.length;
  const fp = FOCUS_RING[state.focusIdx];
  state.centerLat = fp.lat;
  state.centerLon = fp.lon;
  state.centerLabel = fp.label;
  state.rangeIdx = fp.defaultRangeIdx;
  notify();
}

export function setCenter(lat: number, lon: number, label: string): void {
  state.centerLat = lat;
  state.centerLon = lon;
  state.centerLabel = label;
  state.focusIdx = -1;   // typeahead picks aren't part of the ring
  notify();
}

export function setView(v: ViewMode): void {
  state.view = v;
  notify();
}

export function toggleView(): void {
  state.view = state.view === "radar" ? "weather" : "radar";
  notify();
}

export function toggleLayer(id: LayerId): boolean {
  state.layers[id] = !state.layers[id];
  notify();
  return state.layers[id];
}

export function currentOuterKm(): number {
  return RANGE_PRESETS[state.rangeIdx].outerKm;
}

export function currentRangeLabel(): string {
  return `${RANGE_PRESETS[state.rangeIdx].nm}nm`;
}
