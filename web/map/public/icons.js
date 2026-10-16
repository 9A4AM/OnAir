// icons.js - renders the SVG-path shapes (shapes.js) to canvases / MapLibre images, lazily and cached.
// Image ids look like "narrow|e6e8ea" (shape | colour hex without #); MapLibre asks for missing ones through
// the `styleimagemissing` event, so only icons that are actually on screen are ever drawn.

import { shapeOf } from "./shapes.js";

export const ICON_PX = 96;       // canvas size; the box is -32..32 units
export const PIXEL_RATIO = 3;    // 96 px at pixelRatio 3 = 32 CSS px at icon-size 1, crisp on 2x/3x screens
const OUTLINE = "#0b0c0d";

const pathCache = new Map();
/** Path2D for an SVG path string (cached). */
function path2d(d) {
  let p = pathCache.get(d);
  if (!p) { p = new Path2D(d); pathCache.set(d, p); }
  return p;
}

/** Draw a shape into a fresh canvas: dark halo outline, coloured body, inner detail strokes. */
export function drawShape(shapeName, color, px = ICON_PX) {
  const sh = shapeOf(shapeName);
  const c = document.createElement("canvas");
  c.width = c.height = px;
  const g = c.getContext("2d");
  g.translate(px / 2, px / 2);
  g.scale(px / 64, px / 64);
  g.lineJoin = "round";
  g.lineCap = "round";
  const bodies = sh.body.map(path2d);
  const lines = (sh.lines || []).map(path2d);
  // outline first (contrast on the dark map), under everything
  g.strokeStyle = OUTLINE;
  g.lineWidth = 4.6;
  for (const p of bodies) g.stroke(p);
  g.lineWidth = 3.4;
  for (const p of lines) g.stroke(p);
  g.fillStyle = color;
  g.fill(bodies[0]);
  for (let i = 1; i < bodies.length; i++) g.fill(bodies[i]);
  g.strokeStyle = color;
  g.lineWidth = 1.7;
  for (const p of lines) g.stroke(p);
  g.strokeStyle = "rgba(10,12,14,0.62)";
  g.lineWidth = 1.1;
  for (const d of sh.detail || []) g.stroke(path2d(d));
  return c;
}

/** Canvas to the {width,height,data} form MapLibre addImage accepts. */
export function asImage(c) {
  const img = c.getContext("2d").getImageData(0, 0, c.width, c.height);
  return { width: img.width, height: img.height, data: new Uint8Array(img.data) };
}

/** Parse an image id into [shape, "#rrggbb"]. */
export function parseIconId(id) {
  const [shape, hex] = String(id).split("|");
  return [shape, "#" + (hex || "c8cacc")];
}

/** Compose an image id. `color` may be "#rrggbb". */
export function iconId(shape, color) {
  return shape + "|" + String(color).replace("#", "").toLowerCase();
}

/** Register the lazy loader: MapLibre calls it for any image id it does not have yet. */
export function installLazyIcons(map) {
  map.on("styleimagemissing", (e) => {
    if (!String(e.id).includes("|") || map.hasImage(e.id)) return;
    const [shape, color] = parseIconId(e.id);
    try { map.addImage(e.id, asImage(drawShape(shape, color)), { pixelRatio: PIXEL_RATIO }); } catch { /* map gone */ }
  });
}

/** Paint a shape into a small DOM canvas (layer list, key) at device resolution. */
export function paintMini(canvas, shapeName, color, cssPx = 18) {
  const dpr = Math.min(3, window.devicePixelRatio || 1);
  const px = Math.round(cssPx * dpr);
  canvas.width = canvas.height = px;
  canvas.style.width = canvas.style.height = cssPx + "px";
  canvas.getContext("2d").drawImage(drawShape(shapeName, color, ICON_PX), 0, 0, px, px);
}
