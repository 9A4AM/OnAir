const KINDS = [
  { id: "aircraft", name: "Aircraft", color: "#e6e8ea", rotate: true },
  { id: "vessel", name: "Ships", color: "#8cb8eb", rotate: true },
  { id: "sonde", name: "Sondes", color: "#cc94c7" },
  { id: "aprs", name: "APRS", color: "#8ccc9e" },
  { id: "ham", name: "Amateur", color: "#8ccc9e" },
  { id: "drone", name: "Drones", color: "#e0ad6b", rotate: true },
  { id: "flarm", name: "FLARM", color: "#6ccc7a", rotate: true },
  { id: "sat", name: "Satellites", color: "#c0c2c4" },
  { id: "buoy", name: "Buoys", color: "#7aa8d4" },
  { id: "dsc", name: "DSC", color: "#e0ad6b" },
  { id: "station", name: "Stations", color: "#e0d48f" },
];

const on = Object.fromEntries(KINDS.map((k) => [k.id, true]));
const kindOf = Object.fromEntries(KINDS.map((k) => [k.id, k]));
let tracks = new Map();
let selected = null;
let liveOk = false;
let stations = 0;
let serverLoad = null;
let query = "";

const map = new maplibregl.Map({
  container: "map",
  style: {
    version: 8,
    glyphs: "https://demotiles.maplibre.org/font/{fontstack}/{range}.pbf",
    sources: {
      osm: {
        type: "raster",
        tiles: [location.origin + "/tiles/{z}/{x}/{y}.png"],
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

function iconCanvas(color, draw) {
  const s = 64;
  const c = document.createElement("canvas");
  c.width = s;
  c.height = s;
  const g = c.getContext("2d");
  g.translate(s / 2, s / 2);
  g.lineJoin = "round";
  g.lineCap = "round";
  draw(g);
  g.fillStyle = color;
  g.strokeStyle = "#101112";
  g.lineWidth = 3.2;
  g.stroke();
  g.fill();
  return c;
}

function asImage(c) {
  const img = c.getContext("2d").getImageData(0, 0, c.width, c.height);
  return { width: img.width, height: img.height, data: new Uint8Array(img.data) };
}

function icons(colorOfKind) {
  const col = (id) => colorOfKind(id);
  return {
    aircraft: iconCanvas(col("aircraft"), (g) => {
      g.beginPath();
      g.moveTo(0, -26);
      g.lineTo(3.2, -10);
      g.lineTo(24, 5);
      g.lineTo(24, 10);
      g.lineTo(3.5, 3);
      g.lineTo(4.2, 16);
      g.lineTo(11, 23);
      g.lineTo(11, 26);
      g.lineTo(0, 20);
      g.lineTo(-11, 26);
      g.lineTo(-11, 23);
      g.lineTo(-4.2, 16);
      g.lineTo(-3.5, 3);
      g.lineTo(-24, 10);
      g.lineTo(-24, 5);
      g.lineTo(-3.2, -10);
      g.closePath();
    }),
    glider: iconCanvas(SRC.flarm.color, (g) => {
      g.beginPath();
      g.moveTo(0, -26);
      g.lineTo(2, -6);
      g.lineTo(28, 2);
      g.lineTo(28, 7);
      g.lineTo(2, 3);
      g.lineTo(2, 16);
      g.lineTo(-2, 16);
      g.lineTo(-2, 3);
      g.lineTo(-28, 7);
      g.lineTo(-28, 2);
      g.lineTo(-2, -6);
      g.closePath();
    }),
    vessel: iconCanvas(col("vessel"), (g) => {
      g.beginPath();
      g.moveTo(0, -26);
      g.lineTo(8, -10);
      g.lineTo(11, 4);
      g.lineTo(9, 20);
      g.lineTo(5, 26);
      g.lineTo(-5, 26);
      g.lineTo(-9, 20);
      g.lineTo(-11, 4);
      g.lineTo(-8, -10);
      g.closePath();
    }),
    sonde: iconCanvas(col("sonde"), (g) => {
      g.beginPath();
      g.arc(0, -8, 14, 0, Math.PI * 2);
      g.moveTo(-5, 16);
      g.lineTo(-5, 24);
      g.lineTo(5, 24);
      g.lineTo(5, 16);
      g.closePath();
    }),
    aprs: iconCanvas(col("aprs"), (g) => {
      g.beginPath();
      g.moveTo(0, -22);
      g.lineTo(22, 0);
      g.lineTo(0, 22);
      g.lineTo(-22, 0);
      g.closePath();
    }),
    ham: iconCanvas(col("ham"), (g) => {
      g.beginPath();
      g.moveTo(-2, -24);
      g.lineTo(2, -24);
      g.lineTo(2, 16);
      g.lineTo(12, 26);
      g.lineTo(-12, 26);
      g.lineTo(-2, 16);
      g.closePath();
    }),
    drone: iconCanvas(col("drone"), (g) => {
      g.beginPath();
      g.arc(-16, -16, 7, 0, Math.PI * 2);
      g.moveTo(16, -16); g.arc(16, -16, 7, 0, Math.PI * 2);
      g.moveTo(-16, 16); g.arc(-16, 16, 7, 0, Math.PI * 2);
      g.moveTo(16, 16); g.arc(16, 16, 7, 0, Math.PI * 2);
      g.rect(-5, -5, 10, 10);
    }),
    flarm: iconCanvas(col("flarm"), (g) => {
      g.beginPath();
      g.moveTo(0, -24);
      g.lineTo(3, -4);
      g.lineTo(26, 6);
      g.lineTo(26, 10);
      g.lineTo(2, 4);
      g.lineTo(3, 22);
      g.lineTo(-3, 22);
      g.lineTo(-2, 4);
      g.lineTo(-26, 10);
      g.lineTo(-26, 6);
      g.lineTo(-3, -4);
      g.closePath();
    }),
    sat: iconCanvas(col("sat"), (g) => {
      g.beginPath();
      g.rect(-8, -8, 16, 16);
      g.rect(-26, -5, 16, 10);
      g.rect(10, -5, 16, 10);
    }),
    buoy: iconCanvas(col("buoy"), (g) => {
      g.beginPath();
      g.arc(0, 6, 13, 0, Math.PI * 2);
      g.moveTo(-2, -20);
      g.lineTo(2, -20);
      g.lineTo(2, 0);
      g.lineTo(-2, 0);
      g.closePath();
    }),
    dsc: iconCanvas(col("dsc"), (g) => {
      g.beginPath();
      g.moveTo(-16, 8);
      g.lineTo(-10, -4);
      g.lineTo(10, -4);
      g.lineTo(16, 8);
      g.lineTo(12, 18);
      g.lineTo(-12, 18);
      g.closePath();
    }),
    station: iconCanvas(col("station"), (g) => {
      g.beginPath();
      g.moveTo(0, 22);
      g.lineTo(18, -8);
      g.lineTo(-18, -8);
      g.closePath();
    }),
  };
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

function colorOf(kind, t) {
  if (t) {
    const src = srcOf(t);
    if (src) {
      const spec = SRC[src];
      return ageSec(t) > spec.stale ? STALE : spec.color;
    }
  }
  return (kindOf[kind] || {}).color || "#c8cacc";
}

function iconName(t) {
  const src = srcOf(t);
  if (src === "flarm") return ageSec(t) > SRC.flarm.stale ? "glider-stale" : "glider";
  if (src && (t.kind === "aircraft" || t.kind === "flarm")) {
    const live = ageSec(t) <= SRC[src].stale;
    return live ? "aircraft-" + src : "aircraft-stale";
  }
  return t.kind;
}

function destPoint(t) {
  if (t.heading == null || t.speed == null || t.speed < 20) return null;
  const nm = t.speed * (2 / 60);
  const rad = Math.PI / 180, r = nm / 3440.065, h = t.heading * rad, p1 = t.lat * rad;
  const lat = Math.asin(Math.sin(p1) * Math.cos(r) + Math.cos(p1) * Math.sin(r) * Math.cos(h)) / rad;
  const lon = t.lon + Math.atan2(Math.sin(h) * Math.sin(r) * Math.cos(p1), Math.cos(r) - Math.sin(p1) * Math.sin(lat * rad)) / rad;
  return [lon, lat];
}

function geojson() {
  const features = [];
  for (const t of tracks.values()) {
    if (!on[t.kind]) continue;
    const rot = kindOf[t.kind] && kindOf[t.kind].rotate && t.heading != null;
    features.push({
      type: "Feature",
      id: t.id,
      properties: {
        id: t.id,
        kind: t.kind,
        icon: iconName(t),
        label: t.label || "",
        color: colorOf(t.kind, t),
        heading: rot || srcOf(t) ? (t.heading || 0) : 0,
        sel: t.id === selected ? 1 : 0,
      },
      geometry: { type: "Point", coordinates: [t.lon, t.lat] },
    });
  }
  return { type: "FeatureCollection", features };
}

function trailGeo() {
  const features = [];
  for (const t of tracks.values()) {
    if (!on[t.kind]) continue;
    const path = Array.isArray(t.path) ? t.path.slice() : [];
    if (path.length && (path[path.length - 1][0] !== t.lon || path[path.length - 1][1] !== t.lat)) path.push([t.lon, t.lat]);
    if (path.length >= 2) {
      features.push({
        type: "Feature",
        properties: { id: t.id, color: colorOf(t.kind, t), kind: "trail" },
        geometry: { type: "LineString", coordinates: path },
      });
    }
    const d = destPoint(t);
    if (d) {
      features.push({
        type: "Feature",
        properties: { id: t.id, color: colorOf(t.kind, t), kind: "pred" },
        geometry: { type: "LineString", coordinates: [[t.lon, t.lat], d] },
      });
    }
  }
  return { type: "FeatureCollection", features };
}

function apply() {
  const src = map.getSource("live");
  if (src) src.setData(geojson());
  const tr = map.getSource("trails");
  if (tr) tr.setData(trailGeo());
  renderLayers();
  renderList();
  renderStats();
  document.getElementById("hint").classList.toggle("hide", tracks.size > 0);
  if (selected && tracks.has(selected)) showCard(tracks.get(selected));
  else if (selected && !tracks.has(selected)) {
    document.getElementById("card").hidden = true;
    selected = null;
  }
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
  if (t.heading == null && prev && prev.heading != null) t.heading = prev.heading;
  if (t.speed == null && prev && prev.speed != null) t.speed = prev.speed;
  if (prev && prev.meta && !t.meta) t.meta = prev.meta;
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
  return t;
}

function upsert(list) {
  for (const t of list) tracks.set(t.id, mergePathClient(tracks.get(t.id), t));
  apply();
}

function renderLayers() {
  const counts = {};
  for (const t of tracks.values()) counts[t.kind] = (counts[t.kind] || 0) + 1;
  const el = document.getElementById("layers");
  el.innerHTML = KINDS.map((k) => {
    const n = counts[k.id] || 0;
    return `<label class="${on[k.id] ? "" : "off"}">
      <input type="checkbox" data-k="${k.id}" ${on[k.id] ? "checked" : ""}>
      <canvas class="ico" data-icon="${k.id}"></canvas>
      <span>${k.name}</span>
      <span class="n">${n}</span>
    </label>`;
  }).join("");
  for (const box of el.querySelectorAll("input")) {
    box.onchange = () => { on[box.dataset.k] = box.checked; apply(); };
  }
  paintMiniIcons(el);
}

function paintMiniIcons(root) {
  const set = icons(colorOf);
  for (const c of root.querySelectorAll("canvas[data-icon]")) {
    const src = set[c.dataset.icon];
    if (!src) continue;
    const n = c.width = c.height = 18;
    c.getContext("2d").drawImage(src, 0, 0, n, n);
  }
}

function visibleTracks() {
  const q = query.trim().toLowerCase();
  const out = [];
  for (const t of tracks.values()) {
    if (!on[t.kind]) continue;
    if (q && !(t.label || "").toLowerCase().includes(q) && !t.id.toLowerCase().includes(q) && !(t.sub || "").toLowerCase().includes(q)) continue;
    out.push(t);
  }
  out.sort((a, b) => (a.kind + a.label).localeCompare(b.kind + b.label));
  return out;
}

function renderList() {
  const list = visibleTracks();
  document.getElementById("trackn").textContent = String(list.length);
  const el = document.getElementById("list");
  if (!list.length) {
    el.innerHTML = `<div class="empty">${tracks.size ? "No tracks in this filter." : "Waiting for reports."}</div>`;
    return;
  }
  const now = Date.now();
  el.innerHTML = list.slice(0, 400).map((t) => `
    <button type="button" data-id="${esc(t.id)}" class="${t.id === selected ? "sel" : ""}">
      <canvas class="sym" data-icon="${t.kind}"></canvas>
      <span class="lab">${esc(t.label || t.id)}</span>
      <span class="age">${esc(age(now - t.t))}</span>
      <span class="sub">${esc(cap(t.kind) + (t.sub ? " · " + t.sub : ""))}</span>
    </button>`).join("");
  for (const b of el.querySelectorAll("button")) {
    b.onclick = () => focusTrack(b.dataset.id);
  }
  paintMiniIcons(el);
}

function renderLoad() {
  const el = document.getElementById("load");
  const fill = document.getElementById("loadFill");
  const pctEl = document.getElementById("loadPct");
  if (!el || !fill || !pctEl) return;
  if (!liveOk || !serverLoad || serverLoad.pct == null) {
    el.hidden = true;
    return;
  }
  el.hidden = false;
  const pct = Math.max(0, Math.min(100, Number(serverLoad.pct) || 0));
  fill.style.width = pct + "%";
  pctEl.textContent = pct + "%";
  el.classList.toggle("warn", pct >= 55 && pct < 82);
  el.classList.toggle("hot", pct >= 82);
  const tip = [
    "Shared map load. 100% = very busy; ingests may be slowed.",
    "Load " + pct + "%",
    serverLoad.viewers != null ? serverLoad.viewers + " viewers" : "",
    serverLoad.stations != null ? serverLoad.stations + " stations (30m)" : "",
    serverLoad.tracks != null ? serverLoad.tracks + " tracks stored" : "",
    serverLoad.ingestPerMin != null ? serverLoad.ingestPerMin + " ingests/min" : "",
    serverLoad.throttlePerMin ? serverLoad.throttlePerMin + " rate-limits/min" : "",
  ].filter(Boolean).join(" · ");
  el.title = tip;
}

function renderStats() {
  const pill = document.getElementById("link");
  pill.classList.toggle("ok", liveOk);
  pill.querySelector("span").textContent = liveOk ? (stations ? stations + " stations" : "live") : "offline";
  const counts = {};
  for (const t of tracks.values()) counts[t.kind] = (counts[t.kind] || 0) + 1;
  const bits = KINDS.filter((k) => counts[k.id]).map((k) => counts[k.id] + " " + k.name.toLowerCase());
  document.getElementById("counts").textContent = bits.length ? bits.join("  ·  ") : "no reports";
  renderLoad();
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

const metaCache = new Map();
const metaPending = new Set();

function loadMeta(t) {
  if (t.kind !== "aircraft" && t.kind !== "flarm") return;
  const icao = icaoOf(t);
  const cs = callsignOf(t);
  if (icao.length !== 6 && cs.length < 3) return;
  const k = (icao || "-") + ":" + (cs || "-");
  if (t.meta) return;
  if (metaCache.has(k)) {
    t.meta = metaCache.get(k);
    return;
  }
  if (metaPending.has(k)) return;
  metaPending.add(k);
  const q = new URLSearchParams();
  if (icao.length === 6) q.set("icao", icao);
  if (cs.length >= 3) q.set("cs", cs);
  fetch("/meta?" + q.toString()).then((r) => r.json()).then((m) => {
    metaCache.set(k, m);
    const cur = tracks.get(t.id);
    if (cur) cur.meta = m;
    if (selected === t.id) showCard(tracks.get(t.id) || t);
  }).catch(() => {}).finally(() => metaPending.delete(k));
}

function showCard(t) {
  selected = t.id;
  loadMeta(t);
  const el = document.getElementById("card");
  el.hidden = false;
  const m = t.meta || {};
  const rows = [];
  const type = [m.type, m.icaoType ? "(" + m.icaoType + ")" : ""].filter(Boolean).join(" ").trim();
  if (type) rows.push(["Aircraft", type]);
  else rows.push(["Source", cap(t.kind) + (t.sub ? " · " + t.sub : "")]);
  if (m.registration) rows.push(["Reg", m.registration]);
  if (m.operator) rows.push(["Operator", m.operator]);
  if (m.origin || m.destination) {
    const from = (m.origin || "—") + (m.originName ? "  " + m.originName : "");
    const to = (m.destination || "—") + (m.destinationName ? "  " + m.destinationName : "");
    rows.push(["From", from]);
    rows.push(["To", to]);
  }
  rows.push(["Position", fmtLat(t.lat) + "  " + fmtLon(t.lon)]);
  if (t.cat) rows.push(["Category", t.cat]);
  if (t.squawk != null && t.squawk !== "") rows.push(["Squawk", String(t.squawk).padStart(4, "0")]);
  if (t.alt != null) rows.push(["Altitude", fmtAlt(t)]);
  if (t.heading != null) rows.push(["Heading", String(Math.round(t.heading)).padStart(3, "0") + "°"]);
  if (t.speed != null) rows.push(["Speed", fmtSpeed(t)]);
  if (t.vrate != null) rows.push(["Vertical", Math.round(t.vrate) + " fpm"]);
  if (t.extra) rows.push(["", t.extra]);
  rows.push(["Heard", age(Date.now() - t.t) + " ago"]);
  el.innerHTML = `<button class="x" id="cx" aria-label="Close">×</button>
    <div class="k">${esc(t.sub || cap(t.kind))}</div>
    <h2>${esc(t.label || t.id)}</h2>
    <table>${rows.map(([a, b]) => `<tr><th>${esc(a)}</th><td>${esc(String(b))}</td></tr>`).join("")}</table>`;
  document.getElementById("cx").onclick = (e) => {
    e.stopPropagation();
    el.hidden = true;
    selected = null;
    apply();
  };
  applyListSel();
}

function applyListSel() {
  for (const b of document.querySelectorAll("#list button")) {
    b.classList.toggle("sel", b.dataset.id === selected);
  }
}

function focusTrack(id) {
  const t = tracks.get(id);
  if (!t) return;
  showCard(t);
  apply();
  map.easeTo({ center: [t.lon, t.lat], zoom: Math.max(map.getZoom(), 6.5), duration: 400 });
}

function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s; }

function fmtAlt(t) {
  if (t.kind === "sat") return (t.alt > 50 ? t.alt.toFixed(0) : t.alt.toFixed(1)) + " km";
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

map.on("load", () => {
  map.addSource("trails", { type: "geojson", data: trailGeo() });
  map.addSource("live", { type: "geojson", data: geojson() });
  map.addLayer({
    id: "trail",
    type: "line",
    source: "trails",
    filter: ["==", ["get", "kind"], "trail"],
    paint: { "line-color": ["get", "color"], "line-width": 2, "line-opacity": 0.7 },
  });
  map.addLayer({
    id: "pred",
    type: "line",
    source: "trails",
    filter: ["==", ["get", "kind"], "pred"],
    paint: { "line-color": ["get", "color"], "line-width": 1.2, "line-opacity": 0.4, "line-dasharray": [2, 2] },
  });
  map.addLayer({
    id: "halo",
    type: "circle",
    source: "live",
    filter: [">", ["to-number", ["get", "sel"]], 0],
    paint: {
      "circle-radius": 16,
      "circle-color": "rgba(230,232,234,0.16)",
      "circle-stroke-width": 1.2,
      "circle-stroke-color": "#e6e8ea",
    },
  });
  map.addLayer({
    id: "dot",
    type: "circle",
    source: "live",
    paint: {
      "circle-radius": ["interpolate", ["linear"], ["zoom"], 2, 3, 8, 5],
      "circle-color": ["get", "color"],
      "circle-stroke-width": 1,
      "circle-stroke-color": "#111214",
    },
  });
  try {
    const set = icons(colorOf);
    for (const k of KINDS) {
      if (map.hasImage(k.id)) map.removeImage(k.id);
      map.addImage(k.id, asImage(set[k.id]));
    }
    map.addImage("glider", asImage(set.glider));
    map.addImage("glider-stale", asImage(iconCanvas(STALE, (g) => {
      g.beginPath(); g.moveTo(0, -26); g.lineTo(2, -6); g.lineTo(28, 2); g.lineTo(28, 7); g.lineTo(2, 3); g.lineTo(2, 16);
      g.lineTo(-2, 16); g.lineTo(-2, 3); g.lineTo(-28, 7); g.lineTo(-28, 2); g.lineTo(-2, -6); g.closePath();
    })));
    const plane = (c) => iconCanvas(c, (g) => {
      g.beginPath();
      g.moveTo(0, -26); g.lineTo(3.2, -10); g.lineTo(24, 5); g.lineTo(24, 10); g.lineTo(3.5, 3); g.lineTo(4.2, 16);
      g.lineTo(11, 23); g.lineTo(11, 26); g.lineTo(0, 20); g.lineTo(-11, 26); g.lineTo(-11, 23); g.lineTo(-4.2, 16);
      g.lineTo(-3.5, 3); g.lineTo(-24, 10); g.lineTo(-24, 5); g.lineTo(-3.2, -10); g.closePath();
    });
    map.addImage("aircraft-adsb", asImage(plane(SRC.adsb.color)));
    map.addImage("aircraft-sat", asImage(plane(SRC.sat.color)));
    map.addImage("aircraft-acars", asImage(plane(SRC.acars.color)));
    map.addImage("aircraft-hfdl", asImage(plane(SRC.hfdl.color)));
    map.addImage("aircraft-stale", asImage(plane(STALE)));
  } catch (e) { console.warn("map icons", e); }
  try {
    map.addLayer({
      id: "sym",
      type: "symbol",
      source: "live",
      layout: {
        "icon-image": ["coalesce", ["image", ["get", "icon"]], ["image", ["get", "kind"]], ["image", "aircraft"]],
        "icon-rotate": ["to-number", ["get", "heading"]],
        "icon-rotation-alignment": "map",
        "icon-pitch-alignment": "map",
        "icon-allow-overlap": true,
        "icon-ignore-placement": true,
        "icon-anchor": "center",
        "icon-size": ["interpolate", ["linear"], ["zoom"], 1.5, 0.45, 4, 0.62, 8, 0.82, 12, 1],
      },
    });
  } catch (e) { console.warn("map symbols", e); }
  const hit = ["dot"].concat(map.getLayer("sym") ? ["sym"] : []);
  for (const layer of hit) {
    map.on("click", layer, (e) => {
      const t = tracks.get(e.features[0].properties.id);
      if (t) { showCard(t); apply(); }
    });
    map.on("mouseenter", layer, () => { map.getCanvas().style.cursor = "pointer"; });
    map.on("mouseleave", layer, () => { map.getCanvas().style.cursor = ""; });
  }
  map.on("click", (e) => {
    if (!map.queryRenderedFeatures(e.point, { layers: hit }).length) {
      document.getElementById("card").hidden = true;
      selected = null;
      apply();
    }
  });
  apply();
});

map.on("mousemove", (e) => {
  document.getElementById("cursor").textContent = fmtLat(e.lngLat.lat) + "   " + fmtLon(e.lngLat.lng);
});
map.on("move", () => {
  document.getElementById("zoom").textContent = "z " + map.getZoom().toFixed(1);
});

document.getElementById("q").addEventListener("input", (e) => {
  query = e.target.value;
  renderList();
});
document.getElementById("q").addEventListener("keydown", (e) => {
  if (e.key !== "Enter") return;
  const list = visibleTracks();
  if (list[0]) focusTrack(list[0].id);
});

function tickClock() {
  document.getElementById("clock").textContent = new Date().toISOString().slice(11, 19) + " UTC";
}
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
}

function connect() {
  fetch("/live").then((r) => r.json()).then(takeSnapshot).catch(() => setLive(false));
  const proto = location.protocol === "https:" ? "wss" : "ws";
  let ws;
  try { ws = new WebSocket(proto + "://" + location.host + "/ws"); } catch { return; }
  ws.onopen = () => setLive(true);
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === "hello") takeSnapshot(m);
    else if (m.type === "load" && m.load) { serverLoad = m.load; renderLoad(); }
    else if (m.type === "upsert") upsert(m.tracks || []);
  };
  ws.onclose = () => { setLive(false); setTimeout(connect, 5000); };
}

connect();
renderLayers();
document.getElementById("zoom").textContent = "z " + map.getZoom().toFixed(1);

setInterval(() => {
  fetch("/live").then((r) => r.json()).then(takeSnapshot).catch(() => {});
}, 10000);
