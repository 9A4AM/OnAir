// app.js - OnAir live map: MapLibre map, layers, live feed (WebSocket + /live), search, stats, selection.
// Modules: geo.js (maths), types.js (type -> icon shape), shapes.js + icons.js (map icons), airports.js,
// card.js (info card), demo.js (?demo=1 preview data). Everything here is browser-only glue.

import { iconId, installLazyIcons, paintMini } from "./icons.js";
import { SHAPES } from "./shapes.js";
import { iconShape } from "./types.js";
import { createCard } from "./card.js";
import { generateDemo, stepDemo } from "./demo.js";

const DEMO = new URLSearchParams(location.search).get("demo") === "1";
const REDUCED = window.matchMedia("(prefers-reduced-motion: reduce)").matches;
const $ = (id) => document.getElementById(id);

const KINDS = [
  { id: "aircraft", name: "Aircraft", color: "#e6e8ea", shape: "narrow" },
  { id: "vessel", name: "Ships", color: "#8cb8eb", shape: "ship_cargo" },
  { id: "sonde", name: "Sondes", color: "#cc94c7", shape: "sonde" },
  { id: "aprs", name: "APRS", color: "#8ccc9e", shape: "aprs_car" },
  { id: "ham", name: "Amateur", color: "#8ccc9e", shape: "ham" },
  { id: "drone", name: "Drones", color: "#e0ad6b", shape: "drone" },
  { id: "flarm", name: "FLARM", color: "#6ccc7a", shape: "glider" },
  { id: "sat", name: "Satellites", color: "#c0c2c4", shape: "sat" },
  { id: "buoy", name: "Buoys", color: "#7aa8d4", shape: "buoy" },
  { id: "dsc", name: "DSC", color: "#e0ad6b", shape: "dsc" },
  { id: "station", name: "Stations", color: "#e0d48f", shape: "station" },
  { id: "gnss", name: "GNSS satellites", color: "#9ad0f0", shape: "sat" },
];

const on = Object.fromEntries(KINDS.map((k) => [k.id, true]));
const kindOf = Object.fromEntries(KINDS.map((k) => [k.id, k]));
let tracks = new Map();
let selected = null;
let liveOk = false;
let stations = 0;
let serverLoad = null;
let query = "";
let followId = null;      // id being followed (map kept centred)
let showTrack = true;     // draw the selected track's trail
let demoMeta = {};

const map = new maplibregl.Map({
  container: "map",
  style: {
    version: 8,
    glyphs: "https://demotiles.maplibre.org/font/{fontstack}/{range}.pbf",
    sources: {
      osm: {
        type: "raster",
        tiles: [DEMO && !/on-air\.dev$/.test(location.hostname) ? "https://tile.openstreetmap.org/{z}/{x}/{y}.png" : location.origin + "/tiles/{z}/{x}/{y}.png"],
        tileSize: 256,
        attribution: "© OpenStreetMap",
        maxzoom: 19,
      },
      grid: { type: "geojson", data: graticule() },
    },
    layers: [
      { id: "osm", type: "raster", source: "osm", paint: {
        "raster-saturation": -0.38,
        "raster-contrast": 0.08,
        "raster-brightness-min": 0.08,
        "raster-brightness-max": 0.88,
      } },
      { id: "grid", type: "line", source: "grid", paint: {
        "line-color": "#6a6c70",
        "line-opacity": 0.22,
        "line-width": 0.6,
      } },
    ],
  },
  center: [12, 28],
  zoom: 2.15,
  minZoom: 1.2,
  fadeDuration: 0,
  attributionControl: false,
});
map.addControl(new maplibregl.NavigationControl({ showCompass: false, visualizePitch: false }), "bottom-right");
installLazyIcons(map);

function graticule() {
  const features = [];
  for (let lon = -180; lon <= 180; lon += 15) {
    features.push({ type: "Feature", geometry: { type: "LineString", coordinates: [[lon, -85], [lon, 85]] } });
  }
  for (let lat = -75; lat <= 75; lat += 15) {
    const line = [];
    for (let lon = -180; lon <= 180; lon += 5) line.push([lon, lat]);
    features.push({ type: "Feature", geometry: { type: "LineString", coordinates: line } });
  }
  return { type: "FeatureCollection", features };
}

const SRC = {
  adsb: { color: "#e6e8ea", stale: 30 },
  ais: { color: "#40b0e0", stale: 90 },
  sat: { color: "#5096dc", stale: 60 },
  acars: { color: "#dc4848", stale: 30 },
  hfdl: { color: "#e6c640", stale: 30 },
  flarm: { color: "#6ccc7a", stale: 30 },
  aprs: { color: "#c87848", stale: 120 },
  sonde: { color: "#a878dc", stale: 900 },
  dsc: { color: "#e87840", stale: 600 },
};
const SRC_RANK = { adsb: 40, ais: 38, flarm: 35, acars: 30, hfdl: 28, aprs: 25, ham: 25, sat: 22, sonde: 20, dsc: 18, buoy: 12 };
const STALE = "#969a9e";

function srcOf(t) {
  const s = (t.src || "").toLowerCase();
  if (s && SRC[s]) return s;
  const sub = (t.sub || "").toLowerCase();
  if (t.kind === "flarm" || sub.includes("flarm")) return "flarm";
  if (t.kind === "vessel" || sub.includes("ais")) return "ais";
  if (sub.includes("hfdl")) return "hfdl";
  if (sub.includes("vdl") || sub.includes("acars")) return "acars";
  if (sub.includes("aprs")) return "aprs";
  if (t.kind === "sonde" || sub.includes("sonde")) return "sonde";
  if (t.kind === "dsc" || sub.includes("dsc")) return "dsc";
  if (sub.includes("aero") || sub.includes("inmarsat") || sub.includes("ads-c")) return "sat";
  if (t.kind === "aircraft") return "adsb";
  return "";
}

function ageSec(t) {
  if (Number.isFinite(t.age)) return t.age;
  return (Date.now() - t.t) / 1000;
}

function positionTimeMs(t) {
  const a = Number(t.age);
  if (Number.isFinite(a) && a > 0) return t.t - a * 1000;
  return t.t;
}

// GNSS constellations, by the "sub" the app sends
const GNSS_COLORS = { gps: "#5aa0f0", galileo: "#f0b840", beidou: "#e86060", glonass: "#d070e0", qzss: "#50d0a0", sbas: "#a8b0b8" };

function colorOf(kind, t) {
  if (kind === "gnss") {
    if (t && ageSec(t) > 60) return STALE;
    return GNSS_COLORS[String((t && t.sub) || "").toLowerCase()] || (kindOf.gnss || {}).color;
  }
  if (t) {
    const src = srcOf(t);
    if (src) {
      const spec = SRC[src];
      return ageSec(t) > spec.stale ? STALE : spec.color;
    }
  }
  return (kindOf[kind] || {}).color || "#c8cacc";
}

/** ACARS / VDL2 (and same ingest source) stay a dot until we can point the icon (reported or trail heading). */
function datalinkDotUntilHeading(t, hdg) {
  if (t.kind !== "aircraft") return false;
  const src = srcOf(t);
  return src === "acars" && hdg == null;
}

function pathDistanceM(lon1, lat1, lon2, lat2) {
  const p = Math.PI / 180;
  const dLat = (lat2 - lat1) * p;
  const dLon = (lon2 - lon1) * p;
  const a = Math.sin(dLat / 2) ** 2
    + Math.cos(lat1 * p) * Math.cos(lat2 * p) * Math.sin(dLon / 2) ** 2;
  return 6371000 * 2 * Math.asin(Math.min(1, Math.sqrt(a)));
}

/** Bearing degrees true north from (lon1,lat1) to (lon2,lat2). */
function bearingDeg(lon1, lat1, lon2, lat2) {
  const p = Math.PI / 180;
  const dLon = (lon2 - lon1) * p;
  const y = Math.sin(dLon) * Math.cos(lat2 * p);
  const x = Math.cos(lat1 * p) * Math.sin(lat2 * p) - Math.sin(lat1 * p) * Math.cos(lat2 * p) * Math.cos(dLon);
  return (Math.atan2(y, x) / p + 360) % 360;
}

// Ignore sub-metre jitter; any real trail step (ACARS / VDL2) sets the icon direction.
const MOVE_BEARING_MIN_M = 15;

/** Reported heading when present; else the direction the aircraft is flying (from the trail). */
function effectiveHeading(t) {
  if (t.heading != null && Number.isFinite(t.heading)) return t.heading;
  if (!Number.isFinite(t.lon) || !Number.isFinite(t.lat)) return null;
  const path = Array.isArray(t.path) ? t.path.slice() : [];
  const lon2 = t.lon, lat2 = t.lat;
  if (!path.length || path[path.length - 1][0] !== lon2 || path[path.length - 1][1] !== lat2) path.push([lon2, lat2]);
  if (path.length < 2) return null;
  for (let i = path.length - 2; i >= 0; i--) {
    const [lon1, lat1] = path[i];
    const d = pathDistanceM(lon1, lat1, lon2, lat2);
    if (d >= MOVE_BEARING_MIN_M) return bearingDeg(lon1, lat1, lon2, lat2);
  }
  const [lon0, lat0] = path[0];
  const dTot = pathDistanceM(lon0, lat0, lon2, lat2);
  if (dTot >= MOVE_BEARING_MIN_M) return bearingDeg(lon0, lat0, lon2, lat2);
  return null;
}

/** Great-circle point `nm` nautical miles from (lon, lat) along true heading. */
function aheadPoint(lon, lat, headingDeg, nm) {
  const rad = Math.PI / 180, r = nm / 3440.065, h = headingDeg * rad, p1 = lat * rad;
  const lat2 = Math.asin(Math.sin(p1) * Math.cos(r) + Math.cos(p1) * Math.sin(r) * Math.cos(h)) / rad;
  const lon2 = lon + Math.atan2(Math.sin(h) * Math.sin(r) * Math.cos(p1), Math.cos(r) - Math.sin(p1) * Math.sin(lat2 * rad)) / rad;
  return [lon2, lat2];
}

const EXTRAP_MAX_SEC = {
  adsb: 40, acars: 120, hfdl: 120, sat: 90, flarm: 45, ais: 90, default: 60,
};

function extrapMaxSec(t) {
  const s = srcOf(t);
  return EXTRAP_MAX_SEC[s] || EXTRAP_MAX_SEC.default;
}

function effectiveSpeedKts(t) {
  if (Number.isFinite(t.speed) && t.speed >= 8) return t.speed;
  if (t._prevPos && Number.isFinite(t._prevPosMs)) {
    const dt = (positionTimeMs(t) - t._prevPosMs) / 1000;
    if (dt >= 3 && dt < 900) {
      const [lon1, lat1] = t._prevPos;
      const dM = pathDistanceM(lon1, lat1, t.lon, t.lat);
      const kts = (dM / 1852) / (dt / 3600);
      if (kts >= 8 && kts < 900) return kts;
    }
  }
  return null;
}

/** Where the icon should be drawn now (dead reckoning between fixes). */
function displayLonLat(t, nowMs = Date.now()) {
  const hdg = effectiveHeading(t);
  const spd = effectiveSpeedKts(t);
  if (hdg == null || spd == null) return [t.lon, t.lat];
  const posMs = positionTimeMs(t);
  let dt = (nowMs - posMs) / 1000;
  if (!Number.isFinite(dt) || dt < 0) dt = 0;
  dt = Math.min(dt, extrapMaxSec(t));
  const nm = spd * (dt / 3600);
  if (nm < 0.0005) return [t.lon, t.lat];
  return aheadPoint(t.lon, t.lat, hdg, nm);
}

function destPoint(t, fromLonLat) {
  const hdg = effectiveHeading(t);
  const spd = effectiveSpeedKts(t);
  if (hdg == null || spd == null || spd < 20) return null;
  const [lon, lat] = fromLonLat || displayLonLat(t);
  const nm = spd * (2 / 60);
  return aheadPoint(lon, lat, hdg, nm);
}


/** Shape name (and colour) for a track; the image id "shape|colour" is created lazily by installLazyIcons. */
function shapeFor(t) {
  const ity = (t.meta && t.meta.icaoType) || "";
  const key = t.kind + "|" + (t.cat || "") + "|" + ity + "|" + (t.extra || "") + "|" + (t.sub || "");
  if (t._shapeKey !== key) {
    t._shapeKey = key;
    t._shape = iconShape({ kind: t.kind, cat: t.cat, extra: t.extra, sub: t.sub, label: t.label, icaoType: ity, hex: icaoOf(t) });
  }
  return t._shape;
}
function shapeRotates(shape) { return !!(SHAPES[shape] && SHAPES[shape].rotates); }

function geojson() {
  const features = [];
  for (const t of tracks.values()) {
    if (!on[t.kind]) continue;
    const hdg = effectiveHeading(t);
    const dot = datalinkDotUntilHeading(t, hdg);
    const shape = shapeFor(t);
    const color = colorOf(t.kind, t);
    features.push({
      type: "Feature",
      id: t.id,
      properties: {
        id: t.id,
        kind: t.kind,
        icon: iconId(shape, color),
        label: t.label || "",
        color,
        heading: hdg != null ? hdg : 0,
        sym: dot ? 0 : 1,
        dot: dot ? 1 : 0,
        iconRot: shapeRotates(shape) && hdg != null && !dot ? 1 : 0,
        sel: t.id === selected ? 1 : 0,
        est: t.est ? 1 : 0,   // an estimated (dead-reckoned) position: drawn as a ghost
      },
      geometry: { type: "Point", coordinates: displayLonLat(t) },
    });
  }
  return { type: "FeatureCollection", features };
}

function trailGeo() {
  const features = [];
  if (!selected) return { type: "FeatureCollection", features };
  const t = tracks.get(selected);
  if (!t || !on[t.kind] || t.kind === "gnss") return { type: "FeatureCollection", features };
  const [dlon, dlat] = displayLonLat(t);
  const path = Array.isArray(t.path) ? t.path.slice() : [];
  if (path.length && (path[path.length - 1][0] !== dlon || path[path.length - 1][1] !== dlat)) path.push([dlon, dlat]);
  else if (!path.length) path.push([dlon, dlat]);
  if (showTrack && path.length >= 2) {
    features.push({
      type: "Feature",
      properties: { id: t.id, color: colorOf(t.kind, t), kind: "trail" },
      geometry: { type: "LineString", coordinates: path },
    });
  }
  const d = destPoint(t, [dlon, dlat]);
  if (d) {
    features.push({
      type: "Feature",
      properties: { id: t.id, color: colorOf(t.kind, t), kind: "pred" },
      geometry: { type: "LineString", coordinates: [[dlon, dlat], d] },
    });
  }
  return { type: "FeatureCollection", features };
}

function mergePathClient(prev, t) {
  if (prev && positionTimeMs(t) + 500 < positionTimeMs(prev)) return prev;
  const cur = Array.isArray(t.path) ? t.path.slice() : [];
  const old = prev && Array.isArray(prev.path) ? prev.path.slice() : [];
  let path = cur.length >= 2 ? cur : (old.length ? old : cur);
  if (old.length > path.length) path = old;
  const last = path[path.length - 1];
  if (!last || last[0] !== t.lon || last[1] !== t.lat) path.push([t.lon, t.lat]);
  t.path = path.slice(-80);
  const hdg = effectiveHeading({ ...t, path, heading: t.heading });
  if (hdg != null) t.heading = hdg;
  if (t.heading == null && prev && prev.heading != null) t.heading = prev.heading;
  if (t.speed == null && prev && prev.speed != null) t.speed = prev.speed;
  if (prev && prev.meta && !t.meta) t.meta = prev.meta;
  // first sighting (this page) feeds the "first seen" and departure estimate on the card
  t.first = (prev && prev.first) || t.first || { ms: positionTimeMs(t), altFt: t.kind === "aircraft" ? t.alt : null, lat: t.lat, lon: t.lon };
  if (prev) {
    const prevR = SRC_RANK[srcOf(prev)] || 10;
    const incR = SRC_RANK[srcOf(t)] || 10;
    if (incR < prevR) {
      if (prev.label && !t.label) t.label = prev.label;
      t.sub = prev.sub || t.sub;
      t.src = prev.src || t.src;
    }
    if (prev.squawk && !t.squawk) t.squawk = prev.squawk;
    if (prev.extra && !t.extra) t.extra = prev.extra;
    if (prev.cat && !t.cat) t.cat = prev.cat;
  }
  const moved = !prev || prev.lat !== t.lat || prev.lon !== t.lon;
  if (moved && prev) {
    t._prevPos = [prev.lon, prev.lat];
    t._prevPosMs = positionTimeMs(prev);
  } else if (prev) {
    t._prevPos = prev._prevPos;
    t._prevPosMs = prev._prevPosMs;
  }
  return t;
}

let upsertPending = null;
function upsert(list) {
  if (!upsertPending) upsertPending = [];
  upsertPending.push(...list);
  if (upsertPending._scheduled) return;
  upsertPending._scheduled = true;
  requestAnimationFrame(() => {
    const batch = upsertPending;
    upsertPending = null;
    for (const t of batch) tracks.set(t.id, mergePathClient(tracks.get(t.id), t));
    apply();
  });
}

function icaoOf(t) {
  const m = String(t.id || "").match(/:([0-9A-Fa-f]{6})$/);
  if (m) return m[1].toUpperCase();
  const sub = String(t.sub || "");
  if (/^[0-9A-Fa-f]{6}$/.test(sub)) return sub.toUpperCase();
  return "";
}

function callsignOf(t) {
  return String(t.label || "").toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 8);
}

function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s; }

function fmtAlt(t) {
  if (t.kind === "sat" || t.kind === "gnss") return (t.alt > 50 ? t.alt.toFixed(0) : t.alt.toFixed(1)) + " km";
  if (t.kind === "aircraft") return Math.round(t.alt) + " ft";
  return Math.round(t.alt) + " m";
}

function fmtSpeed(t) {
  if (t.kind === "aircraft" || t.kind === "flarm") return Math.round(t.speed) + " kt";
  if (t.kind === "vessel") return Number(t.speed).toFixed(1) + " kn";
  return Number(t.speed).toFixed(1);
}

function fmtLat(lat) {
  const a = Math.abs(lat).toFixed(4);
  return (lat >= 0 ? a + " N" : a + " S");
}
function fmtLon(lon) {
  const a = Math.abs(lon).toFixed(4);
  return (lon >= 0 ? a + " E" : a + " W");
}

function age(ms) {
  if (!Number.isFinite(ms) || ms < 0) return "—";
  const s = Math.round(ms / 1000);
  if (s < 60) return s + "s";
  if (s < 3600) return Math.round(s / 60) + "m";
  return Math.round(s / 3600) + "h";
}

function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}


// ---------- layers, list, stats ----------

const miniCache = new Map();
/** Data URL of a small icon (cached per shape+colour) so lists use <img> instead of redrawing canvases. */
function miniUrl(shape, color) {
  const k = shape + color;
  let u = miniCache.get(k);
  if (!u) {
    const c = document.createElement("canvas");
    paintMini(c, shape, color, 18);
    u = c.toDataURL();
    miniCache.set(k, u);
  }
  return u;
}

let layersBuilt = false;
/** Build the layer toggles once; later calls only refresh the counts (keeps focus and hover state). */
function renderLayers() {
  const counts = {};
  for (const t of tracks.values()) counts[t.kind] = (counts[t.kind] || 0) + 1;
  const el = $("layers");
  if (!layersBuilt) {
    layersBuilt = true;
    el.innerHTML = KINDS.map((k) => `<label data-k="${k.id}">
      <input type="checkbox" data-k="${k.id}" checked aria-label="${k.name}">
      <img class="ico" alt="" src="${miniUrl(k.shape, k.color)}" width="18" height="18">
      <span>${k.name}</span>
      <span class="n" data-n="${k.id}">0</span>
    </label>`).join("");
    for (const box of el.querySelectorAll("input")) {
      box.onchange = () => { on[box.dataset.k] = box.checked; box.closest("label").classList.toggle("off", !box.checked); apply(); };
    }
  }
  for (const k of KINDS) {
    const n = el.querySelector(`[data-n="${k.id}"]`);
    const v = String(counts[k.id] || 0);
    if (n && n.textContent !== v) n.textContent = v;
  }
}

function matchText(t, q) {
  const m = t.meta || {};
  return [t.label, t.id, t.sub, m.registration, m.flightNumber, m.operator].filter(Boolean).join(" ").toLowerCase().includes(q);
}

function visibleTracks() {
  const q = query.trim().toLowerCase();
  const out = [];
  for (const t of tracks.values()) {
    if (!on[t.kind]) continue;
    if (q && !matchText(t, q)) continue;
    out.push(t);
  }
  out.sort((a, b) => (a.kind + a.label).localeCompare(b.kind + b.label));
  return out;
}

let listTimer = 0;
/** The rail list can hold hundreds of rows, so it re-renders at most once a second. */
function renderListSoon() {
  if (listTimer) return;
  listTimer = setTimeout(() => { listTimer = 0; renderList(); }, 1000);
}

function renderList() {
  const list = visibleTracks();
  $("trackn").textContent = String(list.length);
  const el = $("list");
  if (!list.length) {
    el.innerHTML = `<div class="empty">${tracks.size ? "No tracks match this filter." : (liveOk ? "Waiting for reports…" : "Connecting…")}</div>`;
    return;
  }
  const now = Date.now();
  el.innerHTML = list.slice(0, 400).map((t) => `
    <button type="button" data-id="${esc(t.id)}" class="${t.id === selected ? "sel" : ""}">
      <img class="sym" alt="" src="${miniUrl(shapeFor(t), colorOf(t.kind, t))}" width="18" height="18" loading="lazy">
      <span class="lab">${esc(t.label || t.id)}</span>
      <span class="age">${esc(age(now - t.t))}</span>
      <span class="sub">${esc(cap(t.kind) + (t.sub ? " · " + t.sub : ""))}</span>
    </button>`).join("");
  for (const b of el.querySelectorAll("button")) b.onclick = () => focusTrack(b.dataset.id);
}

function renderLoad() {
  const el = $("load");
  const fill = $("loadFill");
  const pctEl = $("loadPct");
  if (!el || !fill || !pctEl) return;
  if (!liveOk || !serverLoad || serverLoad.pct == null) { el.hidden = true; return; }
  el.hidden = false;
  const pct = Math.max(0, Math.min(100, Number(serverLoad.pct) || 0));
  fill.style.width = pct + "%";
  pctEl.textContent = pct + "%";
  el.classList.toggle("warn", pct >= 55 && pct < 82);
  el.classList.toggle("hot", pct >= 82);
  el.title = [
    "Estimated hub capacity (not CPU). 100% = very busy; ingests may be slowed.",
    "Load " + pct + "%",
    serverLoad.viewers != null ? serverLoad.viewers + " viewers" : "",
    serverLoad.stations != null ? serverLoad.stations + " stations (30m)" : "",
    serverLoad.tracks != null ? serverLoad.tracks + " tracks stored" : "",
    serverLoad.ingestPerMin != null ? serverLoad.ingestPerMin + " ingests/min" : "",
    serverLoad.throttlePerMin ? serverLoad.throttlePerMin + " rate-limits/min" : "",
  ].filter(Boolean).join(" · ");
}

/** Menu bar stats: link pill (stations, viewers), one chip per layer with tracks, load meter. */
function renderStats() {
  const pill = $("link");
  pill.classList.toggle("ok", liveOk);
  const viewers = serverLoad && serverLoad.viewers != null ? serverLoad.viewers : null;
  const label = DEMO ? "demo" : liveOk ? (stations ? stations + " station" + (stations === 1 ? "" : "s") : "live") : "connecting";
  pill.querySelector("span").textContent = label;
  pill.title = DEMO ? "Synthetic demo data" : [liveOk ? "Connected" : "Not connected", stations ? stations + " stations sending (30 min)" : "", viewers != null ? viewers + " viewers" : ""].filter(Boolean).join(" · ");
  const counts = {};
  for (const t of tracks.values()) counts[t.kind] = (counts[t.kind] || 0) + 1;
  const chips = KINDS.filter((k) => counts[k.id]).map((k) =>
    `<span class="chip" title="${esc(k.name)}"><img alt="" src="${miniUrl(k.shape, k.color)}" width="14" height="14">${counts[k.id].toLocaleString()}<span class="chip-n"> ${esc(k.name.toLowerCase())}</span></span>`);
  const html = chips.length ? chips.join("") : `<span class="chip-none">${liveOk ? "no reports" : "…"}</span>`;
  const box = $("counts");
  if (box._html !== html) { box.innerHTML = html; box._html = html; }
  const v = $("viewers");
  if (v) { v.hidden = viewers == null; v.textContent = viewers != null ? viewers + (viewers === 1 ? " viewer" : " viewers") : ""; }
  renderLoad();
}

// ---------- apply / upsert ----------

function apply() {
  const src = map.getSource("live");
  if (src) src.setData(geojson());
  const tr = map.getSource("trails");
  if (tr) tr.setData(trailGeo());
  renderLayers();
  renderListSoon();
  renderStats();
  $("hint").classList.toggle("hide", tracks.size > 0);
  $("overlay").hidden = tracks.size > 0 || liveOk || DEMO;
  if (selected && !tracks.has(selected)) clearSelection();
  tryHash();
}


// ---------- aircraft metadata (route, type, registration) ----------

const metaCache = new Map();
const metaPending = new Set();

/** Ask the worker's /meta (adsbdb) once per aircraft/callsign; in demo mode use the built-in table. */
function loadMeta(t) {
  if (DEMO) { if (demoMeta[t.id]) t.meta = demoMeta[t.id]; return; }
  if (t.kind !== "aircraft" && t.kind !== "flarm") return;
  const icao = icaoOf(t);
  const cs = callsignOf(t);
  if (icao.length !== 6 && cs.length < 3) return;
  const k = (icao || "-") + ":" + (cs || "-");
  if (t.meta) return;
  if (metaCache.has(k)) { t.meta = metaCache.get(k); return; }
  if (metaPending.has(k)) return;
  metaPending.add(k);
  const q = new URLSearchParams();
  if (icao.length === 6) q.set("icao", icao);
  if (cs.length >= 3) q.set("cs", cs);
  fetch("/meta?" + q.toString()).then((r) => r.json()).then((m) => {
    metaCache.set(k, m);
    const cur = tracks.get(t.id);
    if (cur) cur.meta = m;
    if (selected === t.id) refreshCard();
  }).catch(() => {}).finally(() => metaPending.delete(k));
}

// ---------- selection and the info card ----------

const card = createCard($("card"), {
  demo: DEMO,
  loadPhoto: true,
  onClose: () => clearSelection(),
  onFollow: (v) => { followId = v ? selected : null; },
  onTrack: (v) => { showTrack = v; const tr = map.getSource("trails"); if (tr) tr.setData(trailGeo()); },
  isFollowing: () => followId != null && followId === selected,
  isTrackShown: () => showTrack,
  getDisplayPos: (t) => displayLonLat(t),
});

/** Copy the derived fields the card reads (colour, heading) onto the track. */
function prepForCard(t) {
  t._color = colorOf(t.kind, t);
  t._hdg = effectiveHeading(t);
  return t;
}

function refreshCard() {
  const t = selected && tracks.get(selected);
  if (t) card.update(prepForCard(t));
}

function setHash(t) {
  try {
    const hex = t && t.kind === "aircraft" ? String(t.id).split(":")[1] : "";
    const h = !t ? "" : /^[0-9A-Fa-f]{6}$/.test(hex || "") ? "#hex=" + hex : "#id=" + encodeURIComponent(t.id);
    history.replaceState(null, "", location.pathname + location.search + h);
  } catch { /* sandboxed frame */ }
}

function selectTrack(t) {
  if (followId && followId !== t.id) followId = null;
  selected = t.id;
  loadMeta(t);
  card.open(prepForCard(t));
  setHash(t);
  applyListSel();
  const src = map.getSource("live");
  if (src) src.setData(geojson());
  const tr = map.getSource("trails");
  if (tr) tr.setData(trailGeo());
  if (window.matchMedia("(max-width: 860px)").matches) setRailOpen(false);
}

function clearSelection() {
  selected = null;
  followId = null;
  card.close();
  setHash(null);
  applyListSel();
  const src = map.getSource("live");
  if (src) src.setData(geojson());
  const tr = map.getSource("trails");
  if (tr) tr.setData(trailGeo());
}

function applyListSel() {
  for (const b of document.querySelectorAll("#list button")) b.classList.toggle("sel", b.dataset.id === selected);
}

/** Select a track and fly to it. */
function focusTrack(id) {
  const t = tracks.get(id);
  if (!t) return;
  selectTrack(t);
  const [lon, lat] = displayLonLat(t);
  map.easeTo({ center: [lon, lat], zoom: Math.max(map.getZoom(), 6.5), duration: REDUCED ? 0 : 500 });
}

// card refresh at 1 Hz, regardless of how often positions arrive
setInterval(refreshCard, 1000);

// ---------- deep links: #hex=ABC123 or #id=... ----------

let pendingHash = null;
let pendingSince = 0;
function readHash() {
  const m = location.hash.match(/^#(hex|id)=(.+)$/);
  if (!m) return null;
  return m[1] === "hex" ? { hex: m[2].toUpperCase() } : { id: decodeURIComponent(m[2]) };
}
pendingHash = readHash();
pendingSince = Date.now();
window.addEventListener("hashchange", () => { pendingHash = readHash(); pendingSince = Date.now(); tryHash(); });

/** Open the card for a pending deep link as soon as the track is on the map; tell the user if it never appears. */
function tryHash() {
  if (!pendingHash) return;
  let found = null;
  if (pendingHash.id) found = tracks.get(pendingHash.id);
  else for (const t of tracks.values()) if (t.kind === "aircraft" && String(t.id).toUpperCase().endsWith(":" + pendingHash.hex)) { found = t; break; }
  if (found) { pendingHash = null; focusTrack(found.id); }
  else if (tracks.size && Date.now() - pendingSince > 15000) { pendingHash = null; toast("That track is not on the map right now."); }
}

let toastTimer = 0;
function toast(msg) {
  const el = $("toast");
  el.textContent = msg;
  el.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.hidden = true; }, 4500);
}

// ---------- map layers ----------

map.on("load", () => {
  map.addSource("trails", { type: "geojson", data: trailGeo() });
  map.addSource("live", { type: "geojson", data: geojson() });
  map.addLayer({
    id: "trail", type: "line", source: "trails",
    filter: ["==", ["get", "kind"], "trail"],
    layout: { "line-cap": "round", "line-join": "round" },
    paint: { "line-color": ["get", "color"], "line-width": 2.2, "line-opacity": 0.75 },
  });
  map.addLayer({
    id: "pred", type: "line", source: "trails",
    filter: ["==", ["get", "kind"], "pred"],
    paint: { "line-color": ["get", "color"], "line-width": 1.2, "line-opacity": 0.45, "line-dasharray": [2, 2] },
  });
  // selected state: soft glow plus a ring in the track's colour
  map.addLayer({
    id: "halo", type: "circle", source: "live",
    filter: [">", ["to-number", ["get", "sel"]], 0],
    paint: {
      "circle-radius": 20,
      "circle-color": ["get", "color"],
      "circle-opacity": 0.2,
      "circle-blur": 0.7,
      "circle-stroke-width": 1.5,
      "circle-stroke-color": ["get", "color"],
      "circle-stroke-opacity": 0.9,
    },
  });
  map.addLayer({
    id: "dot", type: "circle", source: "live",
    filter: ["==", ["to-number", ["get", "dot"]], 1],
    paint: {
      "circle-radius": ["interpolate", ["linear"], ["zoom"], 2, 3, 8, 5],
      "circle-color": ["get", "color"],
      "circle-stroke-width": 1.2,
      "circle-stroke-color": "#0b0c0d",
      "circle-opacity": ["case", ["==", ["to-number", ["get", "est"]], 1], 0.5, 1],
    },
  });
  map.addLayer({
    id: "sym", type: "symbol", source: "live",
    filter: ["==", ["to-number", ["get", "sym"]], 1],
    layout: {
      "icon-image": ["get", "icon"],
      "icon-rotate": ["case", ["==", ["to-number", ["get", "iconRot"]], 1], ["to-number", ["get", "heading"]], 0],
      "icon-rotation-alignment": "map",
      "icon-pitch-alignment": "map",
      "icon-allow-overlap": true,
      "icon-ignore-placement": true,
      "icon-anchor": "center",
      "icon-size": ["interpolate", ["linear"], ["zoom"], 1.5, 0.5, 4, 0.7, 8, 0.95, 12, 1.25],
      "symbol-sort-key": ["to-number", ["get", "sel"]],
    },
    paint: { "icon-opacity": ["case", ["==", ["to-number", ["get", "est"]], 1], 0.5, 1] },
  });
  const hit = ["dot", "sym"];
  /** Nearest feature to the click within a 14 px box, so thin icons are easy to hit on touch screens too. */
  map.on("click", (e) => {
    const r = 14;
    const feats = map.queryRenderedFeatures([[e.point.x - r, e.point.y - r], [e.point.x + r, e.point.y + r]], { layers: hit });
    if (!feats.length) {
      if (selected) clearSelection();
      if (window.matchMedia("(max-width: 860px)").matches) setRailOpen(false);
      return;
    }
    let best = null, bd = Infinity;
    for (const f of feats) {
      const p = map.project(f.geometry.coordinates);
      const d = (p.x - e.point.x) ** 2 + (p.y - e.point.y) ** 2;
      if (d < bd) { bd = d; best = f; }
    }
    const t = tracks.get(best.properties.id);
    if (t) selectTrack(t);
  });
  for (const layer of hit) {
    map.on("mouseenter", layer, () => { map.getCanvas().style.cursor = "pointer"; });
    map.on("mouseleave", layer, () => { map.getCanvas().style.cursor = ""; });
  }
  apply();
  ensureMotionLoop();
});

// user panning cancels follow mode
for (const ev of ["dragstart", "wheel", "touchstart"]) {
  map.on(ev, (e) => { if (followId && e.originalEvent) { followId = null; card.syncToggles(); } });
}

map.on("mousemove", (e) => { $("cursor").textContent = fmtLat(e.lngLat.lat) + "   " + fmtLon(e.lngLat.lng); });
map.on("move", () => { $("zoom").textContent = "z " + map.getZoom().toFixed(1); });

function setRailOpen(open) {
  const rail = $("rail");
  const backdrop = $("railBackdrop");
  const btn = $("panelBtn");
  if (!rail) return;
  rail.classList.toggle("open", open);
  if (backdrop) { backdrop.classList.toggle("show", open); backdrop.hidden = !open; }
  if (btn) btn.setAttribute("aria-expanded", open ? "true" : "false");
}
$("panelBtn")?.addEventListener("click", () => setRailOpen(!$("rail")?.classList.contains("open")));
$("railBackdrop")?.addEventListener("click", () => setRailOpen(false));

// ---------- search: callsign, registration, ICAO hex, MMSI ----------

const qEl = $("q");
const resEl = $("results");
let resItems = [];
let resIdx = -1;

/** Rank matches: exact id/label first, then prefix, then substring. Returns at most 8 tracks. */
function searchTracks(q) {
  q = q.trim().toLowerCase();
  if (q.length < 2) return [];
  const scored = [];
  for (const t of tracks.values()) {
    const m = t.meta || {};
    const keys = [t.label, String(t.id).split(":")[1], m.registration, m.flightNumber].filter(Boolean).map((s) => String(s).toLowerCase());
    let score = 0;
    for (const k of keys) {
      if (k === q) score = Math.max(score, 3);
      else if (k.startsWith(q)) score = Math.max(score, 2);
      else if (k.includes(q)) score = Math.max(score, 1);
    }
    if (!score && (t.sub || "").toLowerCase().includes(q)) score = 0.5;
    if (score) scored.push([score, t]);
  }
  scored.sort((a, b) => b[0] - a[0] || String(a[1].label).localeCompare(String(b[1].label)));
  return scored.slice(0, 8).map((x) => x[1]);
}

function renderResults() {
  const q = qEl.value;
  resItems = searchTracks(q);
  resIdx = resItems.length ? 0 : -1;
  if (q.trim().length < 2) { resEl.hidden = true; qEl.setAttribute("aria-expanded", "false"); return; }
  resEl.hidden = false;
  qEl.setAttribute("aria-expanded", "true");
  resEl.innerHTML = resItems.length
    ? resItems.map((t, i) => `<li role="option" id="res${i}" data-id="${esc(t.id)}" aria-selected="${i === 0}"><img alt="" src="${miniUrl(shapeFor(t), colorOf(t.kind, t))}" width="18" height="18"><b>${esc(t.label || t.id)}</b><span>${esc(cap(t.kind) + (t.sub ? " · " + t.sub : ""))}</span></li>`).join("")
    : `<li class="none">No match on the map right now</li>`;
  for (const li of resEl.querySelectorAll("li[data-id]")) li.onmousedown = (e) => { e.preventDefault(); pickResult(li.dataset.id); };
}

function pickResult(id) {
  resEl.hidden = true;
  qEl.setAttribute("aria-expanded", "false");
  qEl.blur();
  focusTrack(id);
}

function moveResult(d) {
  if (!resItems.length) return;
  resIdx = (resIdx + d + resItems.length) % resItems.length;
  [...resEl.children].forEach((li, i) => li.setAttribute("aria-selected", String(i === resIdx)));
  qEl.setAttribute("aria-activedescendant", "res" + resIdx);
}

let qTimer = 0;
qEl.addEventListener("input", () => {
  query = qEl.value;
  renderListSoon();
  clearTimeout(qTimer);
  qTimer = setTimeout(renderResults, 120);
});
qEl.addEventListener("keydown", (e) => {
  if (e.key === "ArrowDown") { e.preventDefault(); moveResult(1); }
  else if (e.key === "ArrowUp") { e.preventDefault(); moveResult(-1); }
  else if (e.key === "Enter") {
    if (!resItems.length) renderResults();
    const t = resItems[Math.max(0, resIdx)];
    if (t) pickResult(t.id); else toast("No match on the map right now.");
  } else if (e.key === "Escape") {
    if (qEl.value) { qEl.value = ""; query = ""; renderResults(); renderListSoon(); }
    else qEl.blur();
    e.stopPropagation();
  }
});
qEl.addEventListener("blur", () => { setTimeout(() => { resEl.hidden = true; }, 150); });

// keyboard shortcuts: / search, Esc close
document.addEventListener("keydown", (e) => {
  const inField = /^(INPUT|TEXTAREA|SELECT)$/.test((e.target && e.target.tagName) || "");
  if (e.key === "/" && !inField && !e.metaKey && !e.ctrlKey) { e.preventDefault(); qEl.focus(); qEl.select(); return; }
  if (e.key === "Escape" && !inField) {
    if (selected) clearSelection();
    else if ($("rail").classList.contains("open")) setRailOpen(false);
  }
});

function tickClock() { $("clock").textContent = new Date().toISOString().slice(11, 19) + " UTC"; }
tickClock();
setInterval(tickClock, 1000);

function setLive(ok) {
  liveOk = ok;
  renderStats();
}

function takeSnapshot(d) {
  const next = new Map();
  for (const t of d.tracks || []) next.set(t.id, mergePathClient(tracks.get(t.id), t));
  tracks = next;
  stations = d.stations || 0;
  if (d.load) serverLoad = d.load;
  apply();
  setLive(true);
  if (tracks.size) ensureMotionLoop();
}

let wsOpen = false;

function connect() {
  const proto = location.protocol === "https:" ? "wss" : "ws";
  let ws;
  try { ws = new WebSocket(proto + "://" + location.host + "/ws"); } catch { return; }
  ws.onopen = () => { wsOpen = true; setLive(true); };
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === "hello") takeSnapshot(m);
    else if (m.type === "load" && m.load) { serverLoad = m.load; renderLoad(); }
    else if (m.type === "upsert") upsert(m.tracks || []);
  };
  ws.onclose = () => {
    wsOpen = false;
    setLive(false);
    setTimeout(connect, 5000);
  };
  setTimeout(() => {
    if (!wsOpen && tracks.size === 0) {
      fetch("/live").then((r) => r.json()).then(takeSnapshot).catch(() => setLive(false));
    }
  }, 6000);
}



// ---------- motion loop: dead reckoning, selected trail, follow mode ----------

let motionTimer = 0;
let pulse = 0;
function refreshMotion() {
  if (document.hidden) return;
  const live = map.getSource("live");
  if (live) live.setData(geojson());
  const tr = map.getSource("trails");
  if (tr && selected) tr.setData(trailGeo());
  if (selected && map.getLayer("halo") && !REDUCED) {
    pulse = (pulse + 0.25) % 4;
    map.setPaintProperty("halo", "circle-radius", 19 + 2 * Math.sin(pulse * Math.PI / 2));
  }
  if (followId && tracks.has(followId)) {
    const [lon, lat] = displayLonLat(tracks.get(followId));
    map.easeTo({ center: [lon, lat], duration: REDUCED ? 0 : 260, easing: (x) => x });
  }
}

function ensureMotionLoop() {
  if (motionTimer) return;
  motionTimer = window.setInterval(refreshMotion, 250);
}

/** Fill the collapsible icon guide (what each shape means). */
function renderIconGuide() {
  const rows = [
    ["narrow", "Narrow-body jet"], ["wide", "Wide-body jet"], ["quad", "Four-engine (A380 / 747)"], ["regional", "Regional / business jet"],
    ["turboprop", "Turboprop"], ["light", "Light aircraft"], ["heli", "Helicopter"], ["military", "Military / fighter"],
    ["glider", "Glider"], ["balloon", "Balloon"], ["drone", "Drone"], ["ship_cargo", "Cargo ship"], ["ship_tanker", "Tanker"],
    ["ship_passenger", "Passenger ship"], ["ship_fishing", "Fishing"], ["ship_sailing", "Sailing"], ["ship_tug", "Tug"],
    ["sonde", "Radiosonde"], ["sat", "Satellite"], ["aprs_car", "APRS vehicle"], ["aprs_house", "APRS home / weather"], ["aprs_digi", "APRS digipeater"],
  ];
  $("iconkey").innerHTML = rows.map(([s, n]) => `<div><img alt="" src="${miniUrl(s, "#c8cacc")}" width="18" height="18">${n}</div>`).join("");
}

// ---------- start ----------

renderLayers();
renderIconGuide();
$("zoom").textContent = "z " + map.getZoom().toFixed(1);

if (DEMO) {
  document.title = "DEMO · " + document.title;
  $("demoBanner").hidden = false;
  const d = generateDemo();
  demoMeta = d.meta;
  for (const t of d.tracks) if (demoMeta[t.id]) t.meta = demoMeta[t.id];
  const list = d.tracks;
  // simple back-trail so selected demo tracks show a line straight away
  for (const t of list) {
    if (t.heading != null && t.speed) {
      const back = [];
      const probe = { ...t, heading: (t.heading + 180) % 360, path: [] };
      for (let i = 0; i < 6; i++) { stepDemo([probe], 90); back.unshift([probe.lon, probe.lat]); }
      t.path = back;
    }
  }
  takeSnapshot({ tracks: list, stations: 3, load: { pct: 12, viewers: 1, stations: 3, tracks: list.length } });
  stepTimer();
  function stepTimer() {
    setInterval(() => { stepDemo([...tracks.values()], 2); apply(); }, 2000);
  }
} else {
  connect();
  // Full snapshots are expensive on the hub; rely on WebSocket when connected.
  setInterval(() => {
    if (wsOpen) return;
    fetch("/live").then((r) => r.json()).then(takeSnapshot).catch(() => {});
  }, 30000);
}
