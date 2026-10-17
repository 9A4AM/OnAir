// theme.js - follows prefers-color-scheme: tints the raster basemap dark or light to match the page, and tells the app
// when the theme flips so icons (outline colour) and label colours can follow.
const mq = window.matchMedia("(prefers-color-scheme: dark)");
const listeners = [];

/** "d" on a dark map, "l" on a light one (the suffix of the sprite ids). */
export function themeKey() { return mq.matches ? "d" : "l"; }

export function applyMapTheme(map) {
  const dark = mq.matches;
  const set = (p, v) => { try { map.setPaintProperty("osm", p, v); } catch { /* style not ready */ } };
  set("raster-saturation", dark ? -0.38 : -0.55);
  set("raster-contrast", dark ? 0.08 : 0);
  set("raster-brightness-min", dark ? 0.08 : 0);
  set("raster-brightness-max", dark ? 0.88 : 1);
  try { map.setPaintProperty("grid", "line-color", dark ? "#6a6c70" : "#8a8f96"); } catch { /* ignore */ }
}

/** Call fn(themeKey) whenever the colour scheme changes. */
export function onThemeChange(fn) { listeners.push(fn); }

export function watchMapTheme(map) {
  const go = () => { applyMapTheme(map); for (const fn of listeners) fn(themeKey()); };
  if (map.isStyleLoaded()) applyMapTheme(map); else map.once("load", () => applyMapTheme(map));
  mq.addEventListener?.("change", go);
}
