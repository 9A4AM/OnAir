const KINDS = new Set([
  "aircraft", "vessel", "sonde", "aprs", "ham", "drone", "flarm", "sat", "buoy", "dsc", "station",
]);

const TTL_MS = {
  aircraft: 15 * 60e3,
  vessel: 30 * 60e3,
  sonde: 3 * 3600e3,
  aprs: 45 * 60e3,
  ham: 45 * 60e3,
  drone: 20 * 60e3,
  flarm: 15 * 60e3,
  sat: 20 * 60e3,
  buoy: 2 * 3600e3,
  dsc: 60 * 60e3,
  station: 6 * 3600e3,
};

function cors(res) {
  const h = new Headers(res.headers);
  h.set("Access-Control-Allow-Origin", "*");
  h.set("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  h.set("Access-Control-Allow-Headers", "Content-Type");
  return new Response(res.body, { status: res.status, headers: h });
}

function json(obj, status = 200) {
  return cors(new Response(JSON.stringify(obj), {
    status,
    headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" },
  }));
}

function ttlFor(kind) {
  return TTL_MS[kind] || 20 * 60e3;
}

/** Soft capacity — public load meter only (not CPU). High caps so a small community stays in single digits. */
const LOAD_CAP = {
  tracks: 15000,
  stations: 500,
  viewers: 2000,
  ingestPerMin: 12000,
  throttlePerMin: 400,
};

const UPSERT_COALESCE_MS = 500;
const SNAPSHOT_CACHE_MS = 4000;
const LOAD_CACHE_MS = 5000;

function pruneTimeWindow(arr, now, windowMs = 60e3) {
  const cut = now - windowMs;
  while (arr.length && arr[0] < cut) arr.shift();
}

function serverLoadPct({ tracks, stations, viewers, ingestPerMin, throttlePerMin }) {
  const ratios = [
    tracks / LOAD_CAP.tracks,
    stations / LOAD_CAP.stations,
    viewers / LOAD_CAP.viewers,
    ingestPerMin / LOAD_CAP.ingestPerMin,
    throttlePerMin / LOAD_CAP.throttlePerMin,
  ];
  const peak = Math.max(0, ...ratios);
  return Math.round(Math.min(100, peak * 100));
}

/** Wall-clock ms of the position report (ingest time minus client age field). */
function positionTimeMs(tMs, ageSec) {
  const a = Number(ageSec);
  if (Number.isFinite(a) && a > 0) return tMs - a * 1000;
  return tMs;
}

function fresherPosition(incomingAge, prevRow, now) {
  if (!prevRow) return true;
  const inT = positionTimeMs(now, incomingAge);
  const prevT = positionTimeMs(prevRow.t, prevRow.age);
  return inT >= prevT - 500;
}

const SRC_RANK = {
  adsb: 40, ais: 38, flarm: 35, acars: 30, hfdl: 28, aprs: 25, ham: 25, sonde: 20, sat: 22, dsc: 18, buoy: 12,
};

/** Max position age (seconds) accepted at ingest — aligned with map UI stale colouring. */
const MAX_INGEST_AGE_SEC = {
  aircraft: { default: 45, adsb: 30, acars: 30, hfdl: 30, sat: 60 },
  vessel: { default: 90, ais: 90 },
  flarm: { default: 30 },
  drone: { default: 45 },
  aprs: { default: 120 },
  ham: { default: 120 },
  sonde: { default: 900 },
  sat: { default: 180 },
  dsc: { default: 600 },
  buoy: { default: 7200 },
  station: { default: 3600 },
};

function maxIngestAgeSec(kind, src) {
  const spec = MAX_INGEST_AGE_SEC[kind];
  if (!spec) return 300;
  const s = String(src || "").toLowerCase();
  if (s && spec[s] != null) return spec[s];
  return spec.default ?? 300;
}

function normalizeIcao(s) {
  const x = String(s || "").toUpperCase().replace(/[^0-9A-F]/g, "");
  return x.length === 6 ? x : "";
}

function normalizeMmsi(s) {
  const x = String(s || "").replace(/\D/g, "");
  if (x.length >= 9) return x.slice(-9);
  return x.length ? x : "";
}

/** Stable client id before kind prefix (ICAO, MMSI, callsign, serial, …). */
function canonicalClientId(kind, raw) {
  const rawId = String(raw.id || "").slice(0, 80);
  if (!rawId) return null;
  switch (kind) {
    case "aircraft": {
      const icao = normalizeIcao(raw.icao) || normalizeIcao(rawId);
      return icao || rawId;
    }
    case "vessel":
    case "dsc": {
      const mmsi = normalizeMmsi(raw.mmsi) || normalizeMmsi(rawId);
      return mmsi || rawId;
    }
    case "flarm": {
      const hex = normalizeIcao(raw.id) || normalizeIcao(rawId);
      return hex || rawId;
    }
    case "aprs":
    case "ham": {
      const cs = rawId.toUpperCase().replace(/[^A-Z0-9-]/g, "").slice(0, 12);
      return cs || rawId;
    }
    default:
      return rawId;
  }
}

function trackStorageId(kind, raw) {
  const id = canonicalClientId(kind, raw);
  if (!id) return null;
  return `${kind}:${id}`;
}

function srcRank(src) {
  return SRC_RANK[String(src || "").toLowerCase()] || 10;
}

/** When merging a fresher fix, keep the better label / source if the update is from a weaker link. */
function mergeTrackMeta(prev, inc) {
  if (!prev) return inc;
  const out = { ...inc };
  const prevR = srcRank(prev.src);
  const incR = srcRank(inc.src);
  if (incR < prevR) {
    out.sub = prev.sub || inc.sub;
    out.src = prev.src || inc.src;
    const weakLabel = !inc.label || inc.label === inc.rawId || inc.label === inc.id;
    if (prev.label && weakLabel) out.label = prev.label;
  }
  if (prev.squawk && !inc.squawk) out.squawk = prev.squawk;
  if (prev.extra && !inc.extra) out.extra = prev.extra;
  if (prev.cat && !inc.cat) out.cat = prev.cat;
  return out;
}

export class MapHub {
  constructor(ctx, env) {
    this.ctx = ctx;
    this.env = env;
    this.sockets = new Set();
    this.ingestAt = [];
    this.throttleAt = [];
    this.lastLoadBroadcast = 0;
    this.lastPrune = 0;
    this.pendingUpserts = new Map();
    this.snapshotCache = null;
    this.snapshotCacheAt = 0;
    this.loadCache = null;
    this.loadCacheAt = 0;
    this.ready = this.ctx.blockConcurrencyWhile(() => this.init());
  }

  invalidateCaches() {
    this.snapshotCache = null;
    this.loadCache = null;
  }

  touchLoad(now, kind) {
    if (kind === "throttle") this.throttleAt.push(now);
    else this.ingestAt.push(now);
    pruneTimeWindow(this.ingestAt, now);
    pruneTimeWindow(this.throttleAt, now);
  }

  loadMetrics(now) {
    pruneTimeWindow(this.ingestAt, now);
    pruneTimeWindow(this.throttleAt, now);
    const viewers = this.ctx.getWebSockets().length;
    const ingestPerMin = this.ingestAt.length;
    const throttlePerMin = this.throttleAt.length;
    if (this.loadCache && now - this.loadCacheAt < LOAD_CACHE_MS) {
      const c = this.loadCache;
      const pct = serverLoadPct({ tracks: c.tracks, stations: c.stations, viewers, ingestPerMin, throttlePerMin });
      return { pct, tracks: c.tracks, stations: c.stations, viewers, ingestPerMin, throttlePerMin };
    }
    const trackRow = this.ctx.storage.sql.exec("SELECT COUNT(*) AS n FROM tracks").toArray()[0];
    const tracks = trackRow ? trackRow.n : 0;
    const st = this.ctx.storage.sql.exec(
      "SELECT COUNT(*) AS n FROM stations WHERE last > ?",
      now - 30 * 60e3,
    ).toArray()[0];
    const stations = st ? st.n : 0;
    this.loadCache = { tracks, stations };
    this.loadCacheAt = now;
    const pct = serverLoadPct({ tracks, stations, viewers, ingestPerMin, throttlePerMin });
    return { pct, tracks, stations, viewers, ingestPerMin, throttlePerMin };
  }

  maybeBroadcastLoad(now) {
    if (now - this.lastLoadBroadcast < 12000) return;
    this.lastLoadBroadcast = now;
    this.broadcast({ type: "load", load: this.loadMetrics(now) });
  }

  async init() {
    this.ctx.storage.sql.exec(`
      CREATE TABLE IF NOT EXISTS tracks (
        id TEXT PRIMARY KEY,
        kind TEXT NOT NULL,
        label TEXT,
        sub TEXT,
        lat REAL NOT NULL,
        lon REAL NOT NULL,
        alt REAL,
        heading REAL,
        speed REAL,
        extra TEXT,
        t INTEGER NOT NULL
      );
      CREATE TABLE IF NOT EXISTS stations (
        id TEXT PRIMARY KEY,
        last INTEGER NOT NULL,
        n INTEGER NOT NULL
      );
    `);
    const cols = this.ctx.storage.sql.exec("PRAGMA table_info(tracks)").toArray().map((r) => r.name);
    const add = (name, typ) => {
      if (!cols.includes(name)) this.ctx.storage.sql.exec(`ALTER TABLE tracks ADD COLUMN ${name} ${typ}`);
    };
    add("squawk", "TEXT");
    add("cat", "TEXT");
    add("vrate", "REAL");
    add("src", "TEXT");
    add("age", "REAL");
    add("path", "TEXT");
    this.ctx.storage.sql.exec(`
      CREATE TABLE IF NOT EXISTS meta (
        k TEXT PRIMARY KEY,
        json TEXT NOT NULL,
        t INTEGER NOT NULL
      );
    `);
    this.ctx.storage.sql.exec("CREATE INDEX IF NOT EXISTS idx_tracks_kind_t ON tracks(kind, t)");
  }

  trackWire(t) {
    return {
      id: t.id, kind: t.kind, label: t.label, sub: t.sub,
      lat: t.lat, lon: t.lon, alt: t.alt, heading: t.heading, speed: t.speed,
      extra: t.extra, t: t.t, squawk: t.squawk, cat: t.cat, vrate: t.vrate,
      src: t.src, age: t.age,
    };
  }

  async queueUpserts(kept) {
    for (const t of kept) this.pendingUpserts.set(t.id, this.trackWire(t));
    const when = Date.now() + UPSERT_COALESCE_MS;
    const cur = await this.ctx.storage.getAlarm();
    if (!cur || cur > when + 50) this.ctx.storage.setAlarm(when);
  }

  flushUpserts() {
    if (!this.pendingUpserts.size) return;
    const tracks = [...this.pendingUpserts.values()];
    this.pendingUpserts.clear();
    this.broadcast({ type: "upsert", tracks });
  }

  async alarm() {
    await this.ready;
    this.flushUpserts();
  }

  async fetch(req) {
    await this.ready;
    const url = new URL(req.url);
    if (req.method === "OPTIONS") return cors(new Response(null, { status: 204 }));
    if (url.pathname === "/ingest" && req.method === "POST") return this.ingest(req);
    if (url.pathname === "/live" && req.method === "GET") return this.live();
    if (url.pathname === "/meta" && req.method === "GET") return this.meta(req);
    if (url.pathname === "/ws") return this.ws(req);
    return json({ error: "not found" }, 404);
  }

  async ingest(req) {
    const ip = req.headers.get("CF-Connecting-IP") || "0";
    let body;
    try {
      body = await req.json();
    } catch {
      return json({ error: "bad json" }, 400);
    }
    const station = String(body.station || "").slice(0, 64);
    if (!/^[a-zA-Z0-9_-]{8,64}$/.test(station)) return json({ error: "station" }, 400);
    const tracks = Array.isArray(body.tracks) ? body.tracks : [];
    if (tracks.length > 250) return json({ error: "too many" }, 413);

    const now = Date.now();
    const prev = this.ctx.storage.sql.exec("SELECT last, n FROM stations WHERE id = ?", station).toArray()[0];
    if (prev && now - prev.last < 1500) {
      this.touchLoad(now, "throttle");
      this.maybeBroadcastLoad(now);
      return json({ error: "slow down" }, 429);
    }

    const kept = [];
    for (const raw of tracks) {
      let t = sanitize(raw, now);
      if (!t) continue;
      const prev = this.ctx.storage.sql.exec(
        "SELECT t, age, path, heading, speed, label, sub, src, squawk FROM tracks WHERE id = ?",
        t.id,
      ).toArray()[0];
      if (!fresherPosition(t.age, prev, now)) continue;
      let merged = prev ? mergeTrackMeta(prev, t) : t;
      merged.path = mergePath(prev && prev.path, merged.path, merged.lon, merged.lat);
      const trailHdg = headingFromTrail(merged.path, merged.lon, merged.lat, merged.heading);
      if (trailHdg != null) merged.heading = trailHdg;
      else if (merged.heading == null && prev && prev.heading != null) merged.heading = prev.heading;
      if (merged.speed == null && prev && prev.speed != null) merged.speed = prev.speed;
      t = merged;
      let pathArr = [];
      try { pathArr = JSON.parse(t.path); } catch { pathArr = []; }
      this.ctx.storage.sql.exec(
        `INSERT INTO tracks (id, kind, label, sub, lat, lon, alt, heading, speed, extra, t, squawk, cat, vrate, src, age, path)
         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
         ON CONFLICT(id) DO UPDATE SET
           kind=excluded.kind, label=excluded.label, sub=excluded.sub,
           lat=excluded.lat, lon=excluded.lon, alt=excluded.alt,
           heading=excluded.heading, speed=excluded.speed, extra=excluded.extra, t=excluded.t,
           squawk=excluded.squawk, cat=excluded.cat, vrate=excluded.vrate, src=excluded.src,
           age=excluded.age, path=excluded.path`,
        t.id, t.kind, t.label, t.sub, t.lat, t.lon, t.alt, t.heading, t.speed, t.extra, t.t,
        t.squawk, t.cat, t.vrate, t.src, t.age, t.path,
      );
      kept.push({ ...t, path: pathArr });
    }
    this.ctx.storage.sql.exec(
      "INSERT INTO stations (id, last, n) VALUES (?, ?, 1) ON CONFLICT(id) DO UPDATE SET last=excluded.last, n=n+1",
      station, now,
    );
    this.pruneIfDue(now);
    this.touchLoad(now, "ingest");
    if (kept.length) {
      this.invalidateCaches();
      await this.queueUpserts(kept);
    }
    this.maybeBroadcastLoad(now);
    return json({ ok: true, n: kept.length, load: this.loadMetrics(now) });
  }

  prune(now) {
    for (const kind of KINDS) {
      this.ctx.storage.sql.exec("DELETE FROM tracks WHERE kind = ? AND t < ?", kind, now - ttlFor(kind));
    }
    this.ctx.storage.sql.exec("DELETE FROM stations WHERE last < ?", now - 24 * 3600e3);
    this.lastPrune = now;
    this.invalidateCaches();
  }

  pruneIfDue(now) {
    if (now - this.lastPrune < 45_000) return;
    this.prune(now);
  }

  snapshot() {
    const now = Date.now();
    this.pruneIfDue(now);
    if (this.snapshotCache && now - this.snapshotCacheAt < SNAPSHOT_CACHE_MS) {
      const cached = this.snapshotCache;
      return { ...cached, load: this.loadMetrics(now) };
    }
    const tracks = [];
    for (const row of this.ctx.storage.sql.exec(
      "SELECT id, kind, label, sub, lat, lon, alt, heading, speed, extra, t, squawk, cat, vrate, src, age FROM tracks",
    )) {
      tracks.push({
        id: row.id, kind: row.kind, label: row.label, sub: row.sub,
        lat: row.lat, lon: row.lon, alt: row.alt, heading: row.heading, speed: row.speed,
        extra: row.extra, t: row.t, squawk: row.squawk, cat: row.cat, vrate: row.vrate,
        src: row.src, age: row.age,
      });
    }
    const st = this.ctx.storage.sql.exec("SELECT COUNT(*) AS n FROM stations WHERE last > ?", now - 30 * 60e3).toArray()[0];
    const body = { now, stations: st ? st.n : 0, tracks };
    this.snapshotCache = body;
    this.snapshotCacheAt = now;
    return { ...body, load: this.loadMetrics(now) };
  }

  live() {
    return json(this.snapshot());
  }

  async meta(req) {
    const url = new URL(req.url);
    const icao = String(url.searchParams.get("icao") || "").toUpperCase().replace(/[^0-9A-F]/g, "").slice(0, 6);
    const cs = String(url.searchParams.get("cs") || "").toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 8);
    if (icao.length !== 6 && cs.length < 3) return json({ error: "query" }, 400);
    const k = (icao || "-") + ":" + (cs || "-");
    const now = Date.now();
    const hit = this.ctx.storage.sql.exec("SELECT json, t FROM meta WHERE k = ?", k).toArray()[0];
    if (hit && now - hit.t < 6 * 3600e3) {
      try { return json(JSON.parse(hit.json)); } catch { /* fetch again */ }
    }
    const out = { type: "", icaoType: "", registration: "", operator: "", origin: "", destination: "", originName: "", destinationName: "" };
    const hdr = { headers: { "User-Agent": "OnAirMap/1.0 (https://on-air.dev; map@on-air.dev)" } };
    const jobs = [];
    if (icao.length === 6) jobs.push(fetch("https://api.adsbdb.com/v0/aircraft/" + icao, hdr).then((r) => r.ok ? r.json() : null).catch(() => null));
    else jobs.push(Promise.resolve(null));
    if (cs.length >= 3) jobs.push(fetch("https://api.adsbdb.com/v0/callsign/" + cs, hdr).then((r) => r.ok ? r.json() : null).catch(() => null));
    else jobs.push(Promise.resolve(null));
    const [ac, fl] = await Promise.all(jobs);
    const a = ac && ac.response && ac.response.aircraft;
    if (a) {
      out.icaoType = String(a.icao_type || "").slice(0, 8);
      const man = String(a.manufacturer || "").trim();
      const typ = String(a.type || "").trim();
      out.type = [man, typ].filter(Boolean).join(" ").slice(0, 80) || out.icaoType;
      out.registration = String(a.registration || "").slice(0, 12);
      out.operator = String(a.registered_owner || "").slice(0, 48);
    }
    const r = fl && fl.response && fl.response.flightroute;
    if (r) {
      out.origin = String((r.origin && (r.origin.iata_code || r.origin.icao_code)) || "").slice(0, 4);
      out.destination = String((r.destination && (r.destination.iata_code || r.destination.icao_code)) || "").slice(0, 4);
      out.originName = String((r.origin && (r.origin.municipality || r.origin.name)) || "").slice(0, 48);
      out.destinationName = String((r.destination && (r.destination.municipality || r.destination.name)) || "").slice(0, 48);
      if (!out.operator && r.airline && r.airline.name) out.operator = String(r.airline.name).slice(0, 48);
    }
    this.ctx.storage.sql.exec(
      "INSERT INTO meta (k, json, t) VALUES (?, ?, ?) ON CONFLICT(k) DO UPDATE SET json=excluded.json, t=excluded.t",
      k, JSON.stringify(out), now,
    );
    return json(out);
  }

  async ws(req) {
    if (req.headers.get("Upgrade") !== "websocket") return json({ error: "ws" }, 426);
    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];
    this.ctx.acceptWebSocket(server);
    const now = Date.now();
    server.send(JSON.stringify({ type: "hello", ...this.snapshot() }));
    this.maybeBroadcastLoad(now);
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage() {}
  webSocketClose() {}
  webSocketError() {}

  broadcast(msg) {
    const s = JSON.stringify(msg);
    for (const ws of this.ctx.getWebSockets()) {
      try { ws.send(s); } catch { /* closed */ }
    }
  }
}

function sanitize(raw, now) {
  if (!raw || typeof raw !== "object") return null;
  const kind = String(raw.kind || "");
  if (!KINDS.has(kind)) return null;
  const lat = Number(raw.lat), lon = Number(raw.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon) || lat < -90 || lat > 90 || lon < -180 || lon > 180) return null;
  if (lat === 0 && lon === 0) return null;
  const storageId = trackStorageId(kind, raw);
  if (!storageId) return null;
  const rawId = String(raw.id || "").slice(0, 80);
  const clientId = canonicalClientId(kind, raw) || rawId;
  const icao = kind === "aircraft" ? (normalizeIcao(raw.icao) || normalizeIcao(clientId)) : "";
  const mmsi = (kind === "vessel" || kind === "dsc") ? (normalizeMmsi(raw.mmsi) || normalizeMmsi(clientId)) : "";
  const alt = Number.isFinite(Number(raw.alt)) ? Number(raw.alt) : null;
  const heading = Number.isFinite(Number(raw.heading)) ? Number(raw.heading) : null;
  const speed = Number.isFinite(Number(raw.speed)) ? Number(raw.speed) : null;
  const vrate = Number.isFinite(Number(raw.vrate)) ? Number(raw.vrate) : null;
  const age = Number.isFinite(Number(raw.age)) ? Number(raw.age) : 0;
  if (age > maxIngestAgeSec(kind, raw.src)) return null;
  const squawk = raw.squawk == null || raw.squawk === "" ? null : String(raw.squawk).slice(0, 8);
  const path = [];
  if (Array.isArray(raw.path)) {
    for (const pt of raw.path.slice(-80)) {
      let la, lo;
      if (Array.isArray(pt) && pt.length >= 2) { la = Number(pt[0]); lo = Number(pt[1]); }
      else if (pt && typeof pt === "object") { la = Number(pt.lat); lo = Number(pt.lon); }
      else continue;
      if (Number.isFinite(la) && Number.isFinite(lo) && Math.abs(la) <= 90 && Math.abs(lo) <= 180) path.push([lo, la]);
    }
  }
  path.push([lon, lat]);
  return {
    id: storageId,
    kind,
    rawId: clientId,
    icao,
    mmsi,
    label: String(raw.label || rawId).slice(0, 48),
    sub: String(raw.sub || "").slice(0, 32),
    lat, lon, alt, heading, speed, vrate, age,
    squawk,
    src: String(raw.src || "").slice(0, 16),
    cat: String(raw.cat || "").slice(0, 48),
    extra: String(raw.extra || "").slice(0, 120),
    path: path.slice(-80),
    t: now,
  };
}

function pathDistanceM(lon1, lat1, lon2, lat2) {
  const p = Math.PI / 180;
  const dLat = (lat2 - lat1) * p;
  const dLon = (lon2 - lon1) * p;
  const a = Math.sin(dLat / 2) ** 2
    + Math.cos(lat1 * p) * Math.cos(lat2 * p) * Math.sin(dLon / 2) ** 2;
  return 6371000 * 2 * Math.asin(Math.min(1, Math.sqrt(a)));
}

function bearingDeg(lon1, lat1, lon2, lat2) {
  const p = Math.PI / 180;
  const dLon = (lon2 - lon1) * p;
  const y = Math.sin(dLon) * Math.cos(lat2 * p);
  const x = Math.cos(lat1 * p) * Math.sin(lat2 * p) - Math.sin(lat1 * p) * Math.cos(lat2 * p) * Math.cos(dLon);
  return (Math.atan2(y, x) / p + 360) % 360;
}

/** Datalink fixes often have no heading field; use the stored trail. */
function headingFromTrail(pathJson, lon, lat, reported) {
  if (reported != null && Number.isFinite(reported)) return reported;
  let path = [];
  try { path = pathJson ? JSON.parse(pathJson) : []; } catch { path = []; }
  if (!Array.isArray(path)) path = [];
  const lon2 = lon, lat2 = lat;
  if (!path.length || path[path.length - 1][0] !== lon2 || path[path.length - 1][1] !== lat2) path.push([lon2, lat2]);
  if (path.length < 2) return null;
  const minM = 15;
  for (let i = path.length - 2; i >= 0; i--) {
    const [lon1, lat1] = path[i];
    if (pathDistanceM(lon1, lat1, lon2, lat2) >= minM) return bearingDeg(lon1, lat1, lon2, lat2);
  }
  const [lon0, lat0] = path[0];
  if (pathDistanceM(lon0, lat0, lon2, lat2) >= minM) return bearingDeg(lon0, lat0, lon2, lat2);
  return null;
}

function mergePath(oldJson, incoming, lon, lat) {
  let old = [];
  try { old = oldJson ? JSON.parse(oldJson) : []; } catch { old = []; }
  if (!Array.isArray(old)) old = [];
  let neu = Array.isArray(incoming) ? incoming.slice() : [];
  if (neu.length <= 1 && old.length >= 2) neu = old.slice();
  else if (old.length > neu.length) neu = old.slice();
  const last = neu[neu.length - 1];
  if (!last || last[0] !== lon || last[1] !== lat) neu.push([lon, lat]);
  return JSON.stringify(neu.slice(-80));
}

async function osmTile(z, x, y) {
  const z0 = z | 0, x0 = x | 0, y0 = y | 0;
  if (!(z0 >= 0 && z0 <= 18) || x0 < 0 || y0 < 0) return new Response("bad tile", { status: 400 });
  const n = 1 << z0;
  if (x0 >= n || y0 >= n) return new Response("bad tile", { status: 400 });
  const upstream = `https://tile.openstreetmap.org/${z0}/${x0}/${y0}.png`;
  const r = await fetch(upstream, {
    headers: { "User-Agent": "OnAirMap/1.0 (https://on-air.dev; map@on-air.dev)" },
    cf: { cacheTtl: 86400, cacheEverything: true },
  });
  if (!r.ok) return new Response("tile", { status: r.status });
  const h = new Headers(r.headers);
  h.set("Cache-Control", "public, max-age=86400");
  h.set("Access-Control-Allow-Origin", "*");
  return new Response(r.body, { status: 200, headers: h });
}

async function lookupAircraft(req) {
  const url = new URL(req.url);
  const icao = String(url.searchParams.get("icao") || "").toUpperCase().replace(/[^0-9A-F]/g, "").slice(0, 6);
  const cs = String(url.searchParams.get("cs") || "").toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 8);
  if (icao.length !== 6 && cs.length < 3) return json({ error: "query" }, 400);
  const cacheKey = new Request("https://map.on-air.dev/meta-cache/" + (icao || "-") + "/" + (cs || "-"));
  const cached = await caches.default.match(cacheKey);
  if (cached) return cors(cached);
  const out = { type: "", icaoType: "", registration: "", operator: "", origin: "", destination: "", originName: "", destinationName: "" };
  const hdr = { headers: { "User-Agent": "OnAirMap/1.0 (https://on-air.dev; map@on-air.dev)" } };
  const jobs = [];
  if (icao.length === 6) jobs.push(fetch("https://api.adsbdb.com/v0/aircraft/" + icao, hdr).then((r) => r.ok ? r.json() : null).catch(() => null));
  else jobs.push(Promise.resolve(null));
  if (cs.length >= 3) jobs.push(fetch("https://api.adsbdb.com/v0/callsign/" + cs, hdr).then((r) => r.ok ? r.json() : null).catch(() => null));
  else jobs.push(Promise.resolve(null));
  const [ac, fl] = await Promise.all(jobs);
  const a = ac && ac.response && ac.response.aircraft;
  if (a) {
    out.icaoType = String(a.icao_type || "").slice(0, 8);
    const man = String(a.manufacturer || "").trim();
    const typ = String(a.type || "").trim();
    out.type = [man, typ].filter(Boolean).join(" ").slice(0, 80) || out.icaoType;
    out.registration = String(a.registration || "").slice(0, 12);
    out.operator = String(a.registered_owner || "").slice(0, 48);
  }
  const r = fl && fl.response && fl.response.flightroute;
  if (r) {
    out.origin = String((r.origin && (r.origin.iata_code || r.origin.icao_code)) || "").slice(0, 4);
    out.destination = String((r.destination && (r.destination.iata_code || r.destination.icao_code)) || "").slice(0, 4);
    out.originName = String((r.origin && (r.origin.municipality || r.origin.name)) || "").slice(0, 48);
    out.destinationName = String((r.destination && (r.destination.municipality || r.destination.name)) || "").slice(0, 48);
    if (!out.operator && r.airline && r.airline.name) out.operator = String(r.airline.name).slice(0, 48);
  }
  const res = json(out);
  const store = new Response(JSON.stringify(out), {
    status: 200,
    headers: { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "public, max-age=21600" },
  });
  await caches.default.put(cacheKey, store);
  return res;
}

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    const tile = url.pathname.match(/^\/tiles\/(\d+)\/(\d+)\/(\d+)\.png$/);
    if (tile) return osmTile(+tile[1], +tile[2], +tile[3]);
    if (url.pathname === "/meta") {
      if (req.method === "OPTIONS") return cors(new Response(null, { status: 204 }));
      if (req.method === "GET") return lookupAircraft(req);
    }
    const api = url.pathname === "/ingest" || url.pathname === "/live" || url.pathname === "/ws";
    if (api) {
      const id = env.MAP.idFromName("hub-v2");
      return env.MAP.get(id).fetch(req);
    }
    return env.ASSETS.fetch(req);
  },
};
