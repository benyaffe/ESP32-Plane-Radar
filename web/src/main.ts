// Plane Radar — web preview entry point.
//
// Skeleton right now: bootstraps the canvas, draws a placeholder ring so
// something's on screen, wires the layer toggle chips. Aircraft +
// coastline + weather etc. land in subsequent commits.

import "./style.css";

const CANVAS_SIZE = 240;

interface LayerDef {
  id: string;
  label: string;
  defaultOn: boolean;
}

const LAYERS: LayerDef[] = [
  { id: "coast", label: "Coast", defaultOn: true },
  { id: "land", label: "Land", defaultOn: true },
  { id: "roads", label: "Roads", defaultOn: true },
  { id: "runways", label: "Runways", defaultOn: true },
  { id: "tags", label: "Tags", defaultOn: true },
];

const enabled = new Map<string, boolean>();

function mountLayers(root: HTMLElement) {
  for (const l of LAYERS) {
    enabled.set(l.id, l.defaultOn);
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = l.label;
    btn.setAttribute("aria-pressed", String(l.defaultOn));
    btn.addEventListener("click", () => {
      const on = !enabled.get(l.id);
      enabled.set(l.id, on);
      btn.setAttribute("aria-pressed", String(on));
      requestFrame();
    });
    root.appendChild(btn);
  }
}

function drawPlaceholder(ctx: CanvasRenderingContext2D) {
  ctx.fillStyle = "#04081c";
  ctx.fillRect(0, 0, CANVAS_SIZE, CANVAS_SIZE);
  ctx.strokeStyle = "#1a5c2e";
  ctx.lineWidth = 1;
  const cx = CANVAS_SIZE / 2;
  const cy = CANVAS_SIZE / 2;
  for (const r of [27, 54, 81, 107]) {
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.moveTo(cx, cy - 107);
  ctx.lineTo(cx, cy + 107);
  ctx.moveTo(cx - 107, cy);
  ctx.lineTo(cx + 107, cy);
  ctx.stroke();
  ctx.fillStyle = "#7a86ad";
  ctx.font = "10px system-ui";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText("scaffolding — content lands soon", cx, cy);
}

let frameQueued = false;
function requestFrame() {
  if (frameQueued) return;
  frameQueued = true;
  requestAnimationFrame(() => {
    frameQueued = false;
    const canvas = document.getElementById("radar") as HTMLCanvasElement | null;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    drawPlaceholder(ctx);
  });
}

function init() {
  const layerRoot = document.getElementById("layer-toggles");
  if (layerRoot) mountLayers(layerRoot);
  requestFrame();
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", init);
} else {
  init();
}
