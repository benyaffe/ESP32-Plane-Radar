// Global application state — center + range + layer toggles. Kept as a
// module-scoped store rather than a full state library because the
// preview has narrow needs.

import { RANGE_PRESETS } from "./theme";

export type LayerId = "coast" | "land" | "roads" | "runways" | "tags";

export interface AppState {
  centerLat: number;
  centerLon: number;
  centerLabel: string;     // "Home" | "SFO" | etc.
  rangeIdx: number;        // index into RANGE_PRESETS
  layers: Record<LayerId, boolean>;
}

// Default center: home (2125 Bryant St SF), 10 nm range — mirrors the
// firmware default.
export const state: AppState = {
  centerLat: 37.759,
  centerLon: -122.409,
  centerLabel: "Home",
  rangeIdx: 1,
  layers: {
    coast: true,
    land: true,
    roads: true,
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

export function setCenter(lat: number, lon: number, label: string): void {
  state.centerLat = lat;
  state.centerLon = lon;
  state.centerLabel = label;
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
