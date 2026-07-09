// Aircraft icons + tags on the radar view.
//
// MVP fidelity: nose-oriented triangle icon, short track vector line,
// two-line data block (callsign / altitude in hundreds of feet) offset
// to a placement that dodges obvious icon overlaps. Skipped for
// simplicity (may add later): trend triangle, emergency squawk styling,
// tag budget, sophisticated two-pass label placement.

import type { Aircraft } from "./aircraft";
import type { ViewFrame } from "./projection";
import { project, distSqFromCenter } from "./projection";
import {
  COLORS,
  GRID_OUTER_RADIUS,
} from "./theme";

interface TagRect {
  x: number; y: number;
  w: number; h: number;
}

const ICON_HALF = 6;                 // half the triangle bbox
const TAG_WIDTH = 42;
const TAG_HEIGHT = 22;
const TAG_LINE_HEIGHT = 10;
const TAG_OFFSET = 12;               // px from icon center to tag anchor

// Eight radial slots around the icon; picked in order. Screen coords
// with +x right, +y down.
const SLOTS: [number, number][] = [
  [ 1,  0], [ 1,  1], [ 0,  1], [-1,  1],
  [-1,  0], [-1, -1], [ 0, -1], [ 1, -1],
];

function iconColor(a: Aircraft): string {
  // Emergency squawk always red.
  if (a.squawk === 7500 || a.squawk === 7600 || a.squawk === 7700) {
    return COLORS.emergency;
  }
  return COLORS.aircraft;
}

function drawIcon(
  ctx: CanvasRenderingContext2D,
  x: number, y: number,
  headingDeg: number,
  color: string,
): void {
  const rad = ((headingDeg - 90) * Math.PI) / 180;  // 0° heading = N, canvas 0° = E
  const cos = Math.cos(rad);
  const sin = Math.sin(rad);
  // Triangle: nose 6 px ahead, tail two corners 4 px back on either side.
  const noseX = x + Math.round(cos * 6);
  const noseY = y + Math.round(sin * 6);
  const leftX = x + Math.round(cos * -4 - sin * 3);
  const leftY = y + Math.round(sin * -4 + cos * 3);
  const rightX = x + Math.round(cos * -4 + sin * 3);
  const rightY = y + Math.round(sin * -4 - cos * 3);
  ctx.fillStyle = color;
  ctx.beginPath();
  ctx.moveTo(noseX, noseY);
  ctx.lineTo(leftX, leftY);
  ctx.lineTo(rightX, rightY);
  ctx.closePath();
  ctx.fill();
}

function drawTrackVector(
  ctx: CanvasRenderingContext2D,
  x: number, y: number,
  trackDeg: number,
  gsKnots: number,
  view: ViewFrame,
): void {
  if (gsKnots < 40) return;  // ignore taxi speeds
  // Vector length = distance covered in 60 s.
  const kmPerSec = (gsKnots * 1.852) / 3600;
  const km = kmPerSec * 60;
  const px = km * view.pxPerKm;
  const rad = ((trackDeg - 90) * Math.PI) / 180;
  const dx = Math.cos(rad) * px;
  const dy = Math.sin(rad) * px;
  ctx.strokeStyle = COLORS.trackVector;
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(x, y);
  ctx.lineTo(x + dx, y + dy);
  ctx.stroke();
}

function rectsOverlap(a: TagRect, b: TagRect): boolean {
  return !(a.x + a.w <= b.x ||
           b.x + b.w <= a.x ||
           a.y + a.h <= b.y ||
           b.y + b.h <= a.y);
}

function pickTagRect(
  cx: number, cy: number,
  taken: TagRect[],
): TagRect {
  // Try each slot; keep the first that doesn't collide, else fall back
  // to the preferred (0) slot.
  for (const [dx, dy] of SLOTS) {
    const ax = cx + dx * TAG_OFFSET;
    const ay = cy + dy * TAG_OFFSET;
    const rect: TagRect = {
      x: Math.round(ax - TAG_WIDTH / 2),
      y: Math.round(ay - TAG_HEIGHT / 2),
      w: TAG_WIDTH,
      h: TAG_HEIGHT,
    };
    let ok = true;
    for (const t of taken) {
      if (rectsOverlap(rect, t)) { ok = false; break; }
    }
    if (ok) return rect;
  }
  // Overloaded — default to the preferred slot even if colliding.
  const [dx, dy] = SLOTS[0];
  return {
    x: Math.round(cx + dx * TAG_OFFSET - TAG_WIDTH / 2),
    y: Math.round(cy + dy * TAG_OFFSET - TAG_HEIGHT / 2),
    w: TAG_WIDTH,
    h: TAG_HEIGHT,
  };
}

function drawTag(
  ctx: CanvasRenderingContext2D,
  rect: TagRect,
  callsign: string,
  altStr: string,
  emergency: boolean,
): void {
  ctx.font = "bold 8px system-ui, sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "top";
  const cx = rect.x + rect.w / 2;
  ctx.fillStyle = emergency ? COLORS.emergency : COLORS.label;
  ctx.fillText(callsign, cx, rect.y);
  ctx.fillStyle = emergency ? COLORS.emergency : COLORS.tagAltitude;
  ctx.fillText(altStr, cx, rect.y + TAG_LINE_HEIGHT);
}

function formatAlt(altFt: number | null): string {
  if (altFt === null) return "GND";
  const hundreds = Math.round(altFt / 100);
  return String(hundreds).padStart(3, "0");
}

/** Draw all aircraft that project inside the outer disc. Sorted by
 *  distance from center (closest last = on top) so the aircraft you're
 *  watching aren't hidden by peripheral traffic. */
export function drawAircraft(
  ctx: CanvasRenderingContext2D,
  view: ViewFrame,
  aircraft: readonly Aircraft[],
  showTags: boolean,
): void {
  interface Placed {
    icon: [number, number];
    a: Aircraft;
    d2: number;
  }
  const placed: Placed[] = [];
  for (const a of aircraft) {
    const [x, y] = project(view, a.lat, a.lon);
    const d2 = distSqFromCenter(x, y);
    if (d2 > GRID_OUTER_RADIUS * GRID_OUTER_RADIUS) continue;
    placed.push({ icon: [x, y], a, d2 });
  }
  // Farthest first so nearby ones draw over them.
  placed.sort((p, q) => q.d2 - p.d2);

  // Track/heading + icon pass.
  for (const { icon: [x, y], a } of placed) {
    drawTrackVector(ctx, x, y, a.trackDeg, a.gsKnots, view);
    drawIcon(ctx, x, y, a.noseDeg || a.trackDeg, iconColor(a));
  }

  if (!showTags) return;

  // Tag placement — reserve icon rects as taken so tags don't cover
  // adjacent aircraft icons.
  const taken: TagRect[] = placed.map(({ icon: [x, y] }) => ({
    x: x - ICON_HALF, y: y - ICON_HALF,
    w: ICON_HALF * 2, h: ICON_HALF * 2,
  }));
  for (const { icon: [x, y], a } of placed) {
    const rect = pickTagRect(x, y, taken);
    taken.push(rect);
    const emergency = a.squawk === 7500 || a.squawk === 7600 || a.squawk === 7700;
    drawTag(
      ctx, rect,
      a.callsign,
      formatAlt(a.altFt),
      emergency,
    );
  }
}
