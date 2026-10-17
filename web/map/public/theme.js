// theme.js - follows prefers-color-scheme: tints the raster basemap dark or light to match the page.
const mq = window.matchMedia("(prefers-color-scheme: dark)");

export function applyMapTheme(map) {
  const dark = mq.matches;
  const set = (p, v) => { try { map.setPaintProperty("osm", p, v); } catch { /* style not ready */ } };
  set("raster-saturation", dark ? -0.38 : -0.55);
  set("raster-contrast", dark ? 0.08 : 0);
  set("raster-brightness-min", dark ? 0.08 : 0);
  set("raster-brightness-max", dark ? 0.88 : 1);
  try { map.setPaintProperty("grid", "line-color", dark ? "#6a6c70" : "#8a8f96"); } catch { /* ignore */ }
}

export function watchMapTheme(map) {
  const go = () => applyMapTheme(map);
  if (map.isStyleLoaded()) go(); else map.once("load", go);
  mq.addEventListener?.("change", go);
}
