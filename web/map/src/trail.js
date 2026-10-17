// trail.js - pure trail rules for the hub: how long and how many points each kind keeps, thinning,
// the compact wire format and request parsing. No Cloudflare APIs here so node --test can run it.

/** Per kind: windowMs = how far back a trail reaches, max = point cap per track. Kinds not listed keep no trail. */
export const TRAIL_RULES = {
  aircraft: { windowMs: 60 * 60e3, max: 300 },
  flarm: { windowMs: 60 * 60e3, max: 300 },
  drone: { windowMs: 60 * 60e3, max: 300 },
  vessel: { windowMs: 6 * 3600e3, max: 200 },
  sonde: { windowMs: 4 * 3600e3, max: 1000 }, // a whole flight (ascent ~1.5 h + descent), longer than the 3 h track TTL
  aprs: { windowMs: 6 * 3600e3, max: 100 },
  ham: { windowMs: 6 * 3600e3, max: 100 },
  dsc: { windowMs: 6 * 3600e3, max: 100 },
  buoy: { windowMs: 6 * 3600e3, max: 50 },
  // sat: none (orbit lines are drawn elsewhere); station: fixed
};

/** Whole-table cap so storage stays bounded no matter how many tracks arrive; oldest rows go first. */
export const GLOBAL_TRAIL_CAP = 150000;

export const MIN_STEP_M = 100;      // closer than this to the last kept point: skip ...
export const MIN_STEP_MS = 5000;    // ... and so is anything sooner than this after it ...
export const TURN_DEG = 8;          // ... unless the heading changed by at least this (keeps corners)
export const MAX_IDS = 60;          // /trails?ids= batch limit
export const MAX_POINTS_PER_TRACK = 1000;

export function kindOfId(id) {
  const i = String(id || "").indexOf(":");
  return i > 0 ? id.slice(0, i) : "";
}

export function ruleFor(kind) {
  return TRAIL_RULES[kind] || null;
}

export function distanceM(lat1, lon1, lat2, lon2) {
  const p = Math.PI / 180;
  const a = Math.sin(((lat2 - lat1) * p) / 2) ** 2
    + Math.cos(lat1 * p) * Math.cos(lat2 * p) * Math.sin(((lon2 - lon1) * p) / 2) ** 2;
  return 6371000 * 2 * Math.asin(Math.min(1, Math.sqrt(a)));
}

export function headingDelta(a, b) {
  if (a == null || b == null || !Number.isFinite(a) || !Number.isFinite(b)) return 0;
  const d = Math.abs(a - b) % 360;
  return d > 180 ? 360 - d : d;
}

/** Should `pt` ({t,lat,lon,hdg}) be appended after `last` (same shape or null)? */
export function shouldKeep(last, pt) {
  if (!last) return true;
  const dt = pt.t - last.t;
  if (dt <= 0) return false;
  if (dt < MIN_STEP_MS) return false;
  if (headingDelta(last.hdg, pt.hdg) >= TURN_DEG) return true;
  return distanceM(last.lat, last.lon, pt.lat, pt.lon) >= MIN_STEP_M;
}

/** Wire point: [t_ms, lat, lon, alt|null, heading|null], rounded (~1 m, whole feet/degrees) to keep payloads small. */
export function wirePoint(p) {
  return [
    p.t,
    Math.round(p.lat * 1e5) / 1e5,
    Math.round(p.lon * 1e5) / 1e5,
    p.alt == null || !Number.isFinite(p.alt) ? null : Math.round(p.alt),
    p.hdg == null || !Number.isFinite(p.hdg) ? null : Math.round(p.hdg),
  ];
}

/** Points to drop from the front of a track's trail: expired ones, then the oldest beyond the cap. */
export function overflowCount(count, cap) {
  return count > cap * 1.1 ? count - cap : 0; // trim in batches (10 % slack) so we are not deleting on every ingest
}

/** Parse /trail?id= and /trails?ids=a,b (also repeated id=). Returns { ids } (deduped, limited) or null when empty. */
export function parseTrailRequest(searchParams) {
  const raw = [];
  for (const v of searchParams.getAll("id")) raw.push(v);
  for (const v of searchParams.getAll("ids")) raw.push(...String(v).split(","));
  const seen = new Set();
  for (const s of raw) {
    const id = String(s).trim().slice(0, 100);
    if (id && ruleFor(kindOfId(id))) seen.add(id);
    if (seen.size >= MAX_IDS) break;
  }
  return seen.size ? { ids: [...seen] } : null;
}
