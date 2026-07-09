// Plane Radar — web preview entry point.

import "./style.css";
import { loadMapData, type MapData } from "./data";
import { renderFrame } from "./renderer";
import { state, subscribe, cycleRange, toggleLayer, type LayerId } from "./state";
import { makeTapDiscriminator } from "./input";
import { mountTypeahead } from "./airports";

interface LayerDef {
  id: LayerId;
  label: string;
}

const LAYERS: LayerDef[] = [
  { id: "coast", label: "Coast" },
  { id: "land", label: "Land" },
  { id: "roads", label: "Roads" },
  { id: "runways", label: "Runways" },
  { id: "tags", label: "Tags" },
];

let mapData: MapData | null = null;

function drawLoadingState(ctx: CanvasRenderingContext2D, msg: string): void {
  ctx.fillStyle = "rgb(4, 10, 28)";
  ctx.fillRect(0, 0, 240, 240);
  ctx.fillStyle = "rgb(122, 134, 173)";
  ctx.font = "10px system-ui, sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText(msg, 120, 120);
}

let frameQueued = false;
function requestFrame(): void {
  if (frameQueued) return;
  frameQueued = true;
  requestAnimationFrame(() => {
    frameQueued = false;
    const canvas = document.getElementById("radar") as HTMLCanvasElement | null;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    if (mapData) {
      renderFrame(ctx, mapData);
    } else {
      drawLoadingState(ctx, "loading map…");
    }
  });
}

function mountLayerToggles(root: HTMLElement): void {
  for (const l of LAYERS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = l.label;
    btn.setAttribute("aria-pressed", String(state.layers[l.id]));
    btn.addEventListener("click", () => {
      const on = toggleLayer(l.id);
      btn.setAttribute("aria-pressed", String(on));
    });
    root.appendChild(btn);
  }
}

function mountCanvasGestures(canvas: HTMLCanvasElement): void {
  const disc = makeTapDiscriminator((tap) => {
    if (tap === "single") cycleRange();
    else if (tap === "double") {
      // Focus cycling isn't implemented yet — placeholder for now.
      // On phase 2 this'll cycle through a small preset ring (SFO/OAK/...).
    } else if (tap === "triple") {
      // Weather view — implemented in a later commit.
    }
  });
  canvas.addEventListener("click", () => disc.tap());
  // Prevent iOS Safari's tap-highlight color from flashing on quick taps.
  canvas.style.setProperty("-webkit-tap-highlight-color", "transparent");
}

function mountKeyboardShortcuts(): void {
  window.addEventListener("keydown", (e) => {
    if (e.target instanceof HTMLInputElement) return;  // don't hijack typing
    if (e.key === " " || e.code === "Space") {
      e.preventDefault();
      // Space = same as canvas tap.
      window.dispatchEvent(new CustomEvent("radar-tap"));
    } else if (["1", "2", "3", "4", "5"].includes(e.key)) {
      const idx = Number(e.key) - 1;
      const id = LAYERS[idx]?.id;
      if (id) {
        toggleLayer(id);
        // Update the button aria-pressed state
        const btns = document.querySelectorAll("#layer-toggles button");
        const btn = btns[idx] as HTMLButtonElement | undefined;
        if (btn) btn.setAttribute("aria-pressed", String(state.layers[id]));
      }
    }
  });
  const spaceDisc = makeTapDiscriminator((tap) => {
    if (tap === "single") cycleRange();
  });
  window.addEventListener("radar-tap", () => spaceDisc.tap());
}

async function init(): Promise<void> {
  requestFrame();  // "loading map…"

  const canvas = document.getElementById("radar") as HTMLCanvasElement | null;
  const layerRoot = document.getElementById("layer-toggles");
  const search = document.getElementById("airport-search") as HTMLInputElement | null;
  const results = document.getElementById("airport-results");

  if (canvas) mountCanvasGestures(canvas);
  if (layerRoot) mountLayerToggles(layerRoot);
  mountKeyboardShortcuts();

  subscribe(requestFrame);

  try {
    mapData = await loadMapData("data");
  } catch (err) {
    console.error(err);
    const canvas = document.getElementById("radar") as HTMLCanvasElement | null;
    const ctx = canvas?.getContext("2d");
    if (ctx) drawLoadingState(ctx, "map load failed");
    return;
  }

  requestFrame();

  if (search && results && mapData) {
    mountTypeahead({
      input: search,
      results,
      index: mapData.airportIndex,
      onSelected: () => requestFrame(),
    });
  }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
