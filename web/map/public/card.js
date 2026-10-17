// card.js - the track info card (slide-in panel on desktop, bottom sheet on phones), FlightRadar24 style.
// build once per selection, then update() rewrites only the text of changed fields (call it at most 1 Hz).
// Pure helpers live in geo.js / types.js; this file is the only part that touches the DOM for the card.

import { SHAPES } from "./shapes.js";
import { airportByCode } from "./airports.js";
import {
  distanceNm, eta, progress, estimateDeparture, squawkMeaning, formatDuration, vrateArrow, flagEmoji,
  hhmm, splitCallsign, badgeColor, isOnGround,
} from "./geo.js";
import { iconShape, FAMILY_NAME } from "./types.js";

const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const cap = (s) => (s ? s.charAt(0).toUpperCase() + s.slice(1) : s);

const SRC_NAME = { adsb: "ADS-B", acars: "ACARS / VDL2", hfdl: "HFDL", flarm: "FLARM", sat: "Satellite (Aero/ADS-C)", ais: "AIS", aprs: "APRS", sonde: "Radiosonde", dsc: "DSC", buoy: "Buoy" };

/** Inline SVG of one shape (silhouette illustration). */
function silhouetteSvg(shapeName, color) {
  const sh = SHAPES[shapeName] || SHAPES.narrow;
  const body = sh.body.map((d) => `<path d="${d}"/>`).join("");
  const lines = (sh.lines || []).map((d) => `<path d="${d}" fill="none" stroke="${color}" stroke-width="1.4" stroke-linecap="round"/>`).join("");
  const det = (sh.detail || []).map((d) => `<path d="${d}" fill="none" stroke="rgba(8,10,14,.6)" stroke-width="1" stroke-linecap="round"/>`).join("");
  return `<svg viewBox="-32 -32 64 64" aria-hidden="true"><g fill="${color}" stroke="#0b0c0d" stroke-width="1.2" stroke-linejoin="round">${body}</g>${lines}${det}</svg>`;
}

/** Resolve one end of the route to {code, iata, icao, name, city, iso, lat, lon}; coordinates fall back to the airport table. */
function resolveAirport(meta, side) {
  const r = meta && meta.route && meta.route[side];
  const code = r ? (r.iata || r.icao) : (meta && meta[side]);
  if (!code && !r) return null;
  const tab = airportByCode((r && (r.icao || r.iata)) || code);
  const lat = r && Number.isFinite(r.lat) ? r.lat : tab && tab.lat;
  const lon = r && Number.isFinite(r.lon) ? r.lon : tab && tab.lon;
  return {
    code: (r && (r.iata || r.icao)) || code || "",
    icao: (r && r.icao) || (tab && tab.icao) || "",
    name: (r && r.name) || (tab && tab.name) || (meta && meta[side + "Name"]) || "",
    city: (r && r.city) || (tab && tab.city) || (meta && meta[side + "Name"]) || "",
    iso: (r && r.iso) || (tab && tab.country) || "",
    lat: Number.isFinite(lat) ? lat : null,
    lon: Number.isFinite(lon) ? lon : null,
  };
}

/**
 * Create the card controller.
 * host: the #card element. api: { onClose(), onFollow(bool), onTrack(bool), onFly(), loadPhoto: bool, demo: bool,
 * getDisplayPos(t) -> [lon,lat], isFollowing(), isTrackShown() }.
 */
export function createCard(host, api) {
  let cur = null;       // current track
  let f = {};           // data-f element map
  let photoFor = "";    // hex whose photo request was made

  function field(name) { return f[name]; }
  function set(name, text) { const el = f[name]; if (el && el.textContent !== text) el.textContent = text; }
  function show(name, on) { const el = f[name]; if (el && el.hidden === on) el.hidden = !on; }

  /** Build the static skeleton for a track and fire the one-time photo request for aircraft. */
  function open(t) {
    cur = t;
    const isAir = t.kind === "aircraft";
    host.hidden = false;
    host.classList.add("open");
    host.innerHTML = `
      <div class="grab" aria-hidden="true"></div>
      <button class="x" type="button" data-act="close" aria-label="Close (Esc)" title="Close (Esc)">&times;</button>
      <header class="c-head">
        <span class="badge" data-f="badge" aria-hidden="true"></span>
        <div class="c-title">
          <h2 data-f="title"></h2>
          <div class="c-sub" data-f="subtitle"></div>
        </div>
        <span class="demo-tag" data-f="demo" hidden>DEMO</span>
      </header>
      <div class="c-scroll">
        <section class="route" data-f="route" hidden>
          <div class="rt-ends">
            <div class="rt-end"><div class="rt-code" data-f="oCode"></div><div class="rt-city" data-f="oCity"></div><div class="rt-name" data-f="oName"></div></div>
            <div class="rt-end right"><div class="rt-code" data-f="dCode"></div><div class="rt-city" data-f="dCity"></div><div class="rt-name" data-f="dName"></div></div>
          </div>
          <div class="rt-bar" data-f="bar" role="progressbar" aria-label="Flight progress" aria-valuemin="0" aria-valuemax="100">
            <div class="rt-fill" data-f="fill"></div>
            <div class="rt-plane" data-f="planeMark"></div>
          </div>
          <div class="rt-dist"><span data-f="flown"></span><span data-f="remain"></span></div>
          <dl class="times">
            <div data-f="depRow" hidden><dt>Departed</dt><dd data-f="dep"></dd></div>
            <div data-f="seenRow"><dt>First seen</dt><dd data-f="seen"></dd></div>
            <div data-f="etaRow"><dt>Arrival</dt><dd data-f="eta"></dd></div>
          </dl>
        </section>
        <figure class="pic" data-f="pic">
          <div class="pic-art" data-f="art"></div>
          <img class="pic-photo" data-f="photo" alt="" hidden referrerpolicy="no-referrer" decoding="async">
          <figcaption><span data-f="picName"></span><span class="pic-credit" data-f="credit" hidden></span></figcaption>
        </figure>
        <dl class="rows" data-f="rows"></dl>
        <p class="c-extra" data-f="extra" hidden></p>
      </div>
      <footer class="c-actions">
        <button type="button" class="tg" data-act="follow" aria-pressed="false">Follow</button>
        <button type="button" class="tg" data-act="track" aria-pressed="true">Track</button>
        <button type="button" data-act="copy" title="Copy a link that opens this card">Copy link</button>
        <button type="button" data-act="share" hidden>Share</button>
      </footer>`;
    f = {};
    for (const el of host.querySelectorAll("[data-f]")) f[el.dataset.f] = el;
    host.querySelector("[data-act=close]").onclick = () => api.onClose();
    host.querySelector("[data-act=follow]").onclick = (e) => { api.onFollow(!api.isFollowing()); syncToggles(); };
    host.querySelector("[data-act=track]").onclick = () => { api.onTrack(!api.isTrackShown()); syncToggles(); };
    host.querySelector("[data-act=copy]").onclick = (e) => copyLink(e.currentTarget);
    const share = host.querySelector("[data-act=share]");
    if (navigator.share) { share.hidden = false; share.onclick = () => navigator.share({ title: document.title, url: linkFor(cur) }).catch(() => {}); }
    show("demo", !!api.demo);
    show("rows", true);
    photoFor = "";
    renderStatic(t);
    update(t);
    syncToggles();
    maybeLoadPhoto(t);
    host.querySelector(".x").focus({ preventScroll: true });
  }

  function syncToggles() {
    const a = host.querySelector("[data-act=follow]"), b = host.querySelector("[data-act=track]");
    if (a) a.setAttribute("aria-pressed", api.isFollowing() ? "true" : "false");
    if (b) b.setAttribute("aria-pressed", api.isTrackShown() ? "true" : "false");
  }

  function linkFor(t) {
    const hex = t.kind === "aircraft" ? (String(t.id).split(":")[1] || "") : "";
    const u = new URL(location.href);
    u.hash = hex && /^[0-9A-Fa-f]{6}$/.test(hex) ? "hex=" + hex : "id=" + encodeURIComponent(t.id);
    return u.toString();
  }

  function copyLink(btn) {
    const url = linkFor(cur);
    const done = () => { btn.textContent = "Copied"; setTimeout(() => { btn.textContent = "Copy link"; }, 1500); };
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(url).then(done, () => window.prompt("Copy this link", url));
    else window.prompt("Copy this link", url);
  }

  /** Parts that do not change at 1 Hz: badge colour, illustration. */
  function renderStatic(t) {
    const src = (t.src || "").toLowerCase();
    const color = t._color || "#e6e8ea";
    const shape = iconShape(t);
    f.art.innerHTML = silhouetteSvg(shape, color);
    f.art.dataset.shape = shape;
    f.pic.classList.toggle("generic", t.kind !== "aircraft");
  }

  /** Look up a photo of the aircraft once per card: by ICAO hex first, then by registration (Planespotters has either). Any failure leaves the illustration. */
  function maybeLoadPhoto(t) {
    if (t.kind !== "aircraft" || api.demo || !api.loadPhoto) return;
    const hex = String(t.id).split(":")[1] || "";
    if (!/^[0-9A-Fa-f]{6}$/.test(hex) || photoFor === hex) return;
    photoFor = hex;
    const first = fetchPhotoInfo("hex/" + hex.toUpperCase());
    // the registration is only tried when the hex gives no photo; it comes from the route/type lookup, which usually has arrived by then
    first
      .then((p) => {
        if (p) return p;
        const reg = String(((cur && cur.meta) || t.meta || {}).registration || "").trim();
        return reg ? fetchPhotoInfo("reg/" + encodeURIComponent(reg)) : null;
      })
      .then((p) => { if (p) showPhoto(p, hex); })
      .catch(() => {});
  }

  /** One Planespotters lookup ('hex/<ICAO>' or 'reg/<registration>'): the first photo, or null. Asked from the visitor's browser (free, keyless). */
  function fetchPhotoInfo(path) {
    return fetch("https://api.planespotters.net/pub/photos/" + path)
      .then((r) => (r.ok ? r.json() : null))
      .then((d) => (d && d.photos && d.photos[0]) || null)
      .catch(() => null);
  }

  /** Show a looked-up photo: it is loaded by an off-screen Image first (a hidden <img> with lazy loading would never load), then swapped in. */
  function showPhoto(p, hex) {
    if (photoFor !== hex || !cur || !String(cur.id).endsWith(hex)) return;
    const src = (p.thumbnail_large && p.thumbnail_large.src) || (p.thumbnail && p.thumbnail.src);
    if (!src || !/^https:\/\//.test(src)) return;
    const pre = new Image();
    pre.referrerPolicy = "no-referrer";
    pre.onload = () => {
      if (photoFor !== hex || !cur) return;
      const img = f.photo;
      img.src = src;
      img.alt = p.photographer ? "Photo by " + p.photographer : "Aircraft photo";
      img.hidden = false;
      f.art.hidden = true;
      const c = f.credit;
      c.hidden = false;
      const who = esc(p.photographer || "unknown");
      const link = /^https:\/\/(www\.)?planespotters\.net\//.test(p.link || "") ? p.link : "https://www.planespotters.net/";
      c.innerHTML = `Photo: <a href="${esc(link)}" target="_blank" rel="noopener noreferrer">${who}</a> / <a href="https://www.planespotters.net/" target="_blank" rel="noopener noreferrer">Planespotters.net</a>`;
    };
    pre.src = src;
  }

  /** Header: callsign, airline badge, flight number. */
  function updateHeader(t, m) {
    const cs = splitCallsign(t.label);
    const airline = (m.airline && m.airline.name) || m.operator || "";
    const code = (m.airline && (m.airline.iata || m.airline.icao)) || (cs && cs.airline) || (t.kind === "aircraft" ? "" : t.kind.slice(0, 3).toUpperCase());
    const bd = f.badge;
    bd.textContent = code.slice(0, 3) || "✈";
    bd.style.background = badgeColor(code || t.kind);
    set("title", t.label || t.id);
    const bits = [];
    if (airline) bits.push(airline);
    if (m.flightNumber && m.flightNumber !== t.label) bits.push("Flight " + m.flightNumber);
    if (!bits.length) bits.push(t.sub || cap(t.kind));
    set("subtitle", bits.join(" · "));
  }

  /** Route block: airports, progress, ETA, departure. */
  function updateRoute(t, m, now) {
    const o = resolveAirport(m, "origin"), d = resolveAirport(m, "destination");
    const has = t.kind === "aircraft" && (o || d);
    show("route", !!has);
    if (!has) return;
    const side = (a, p) => {
      set(p + "Code", a ? a.code || "—" : "—");
      set(p + "City", a ? [flagEmoji(a.iso), a.city].filter(Boolean).join(" ") : "");
      set(p + "Name", a ? (a.name && a.name !== a.city ? a.name : "") : "");
    };
    side(o, "o"); side(d, "d");
    const [lon, lat] = api.getDisplayPos(t);
    const altFt = t.alt, spd = t.speed;
    const pr = o && d && o.lat != null && d.lat != null ? progress(o, d, lat, lon) : null;
    const bar = f.bar;
    if (pr) {
      const pct = Math.round(pr.frac * 100);
      f.fill.style.width = pct + "%";
      f.planeMark.style.left = pct + "%";
      bar.setAttribute("aria-valuenow", String(pct));
      bar.hidden = false;
      set("flown", Math.round(pr.flownNm * 1.852).toLocaleString() + " km flown");
      set("remain", Math.round(pr.remainingNm * 1.852).toLocaleString() + " km to go");
    } else {
      bar.hidden = true;
      set("flown", "");
      set("remain", d && d.lat != null ? Math.round(distanceNm(lat, lon, d.lat, d.lon) * 1.852).toLocaleString() + " km to go" : "");
    }
    // arrival: from remaining great-circle distance and ground speed, clearly an estimate
    if (!d || d.lat == null) {
      set("eta", "Unavailable: destination position unknown");
    } else {
      const e = eta({ lat, lon, altFt, speedKt: spd }, d, now);
      if (!e) set("eta", isOnGround(altFt, spd) ? "On the ground" : "Unavailable: no usable ground speed");
      else set("eta", hhmm(e.atMs) + " local, in " + formatDuration(e.minutes) + " (estimated)");
    }
    // departure: first sighting, and an estimate only when first seen low near the departure airport
    const first = t.first;
    show("seenRow", !!first);
    if (first) set("seen", hhmm(first.ms) + (first.altFt != null ? " at " + Math.round(first.altFt).toLocaleString() + " ft" : ""));
    const dep = o && o.lat != null ? estimateDeparture(first, o) : null;
    show("depRow", !!dep);
    if (dep) set("dep", "about " + hhmm(dep.ms) + " (estimated)");
  }

  /** Data rows for any track. Rebuilt only when their text changes. */
  function updateRows(t, m, now) {
    const rows = [];
    const add = (k, v, cls) => { if (v !== "" && v != null) rows.push([k, String(v), cls || ""]); };
    const src = (t.src || "").toLowerCase();
    if (t.kind === "aircraft" || t.kind === "flarm") {
      add("Registration", m.registration);
      const type = [m.type, m.icaoType ? "(" + m.icaoType + ")" : ""].filter(Boolean).join(" ");
      add("Type", type || (t.cat ? "Category " + t.cat : ""));
      add("ICAO hex", String(t.id).split(":")[1] || "");
    } else {
      add("Source", t.kind === "gnss" ? "GNSS satellite · " + (t.sub || "") + " (decoded broadcast ephemeris)" : cap(t.kind) + (t.sub ? " · " + t.sub : ""));
    }
    if (t.squawk != null && t.squawk !== "") {
      const sq = squawkMeaning(t.squawk);
      add("Squawk", sq.code + (sq.text ? " — " + sq.text : ""), sq.emergency ? "alert" : "");
    }
    if (t.alt != null) {
      const alt = t.kind === "sat" || t.kind === "gnss" ? (t.alt > 50 ? t.alt.toFixed(0) : t.alt.toFixed(1)) + " km"
        : t.kind === "aircraft" ? Math.round(t.alt).toLocaleString() + " ft"
        : Math.round(t.alt).toLocaleString() + " m";
      add(t.kind === "aircraft" ? "Altitude (baro)" : "Altitude", alt);
    }
    if (t.speed != null) {
      const unit = t.kind === "vessel" ? " kn" : t.kind === "aircraft" || t.kind === "flarm" ? " kt" : "";
      const v = t.kind === "vessel" ? Number(t.speed).toFixed(1) : Math.round(t.speed);
      const kmh = t.kind === "aircraft" || t.kind === "flarm" || t.kind === "vessel" ? " (" + Math.round(t.speed * 1.852) + " km/h)" : "";
      add(t.kind === "aircraft" ? "Ground speed" : "Speed", v + unit + kmh);
    }
    if (t.vrate != null) add("Vertical rate", vrateArrow(t.vrate) + " " + Math.round(Math.abs(t.vrate)).toLocaleString() + " ft/min");
    if (t._hdg != null) add("Track", String(Math.round(t._hdg)).padStart(3, "0") + "°" + (t.heading == null ? " (from trail)" : ""));
    add("Position", fmtLat(t.lat) + "  " + fmtLon(t.lon));
    add("Position age", ageText((now - posMs(t)) / 1000));
    if (t.kind === "aircraft" || t.kind === "flarm") add("Source", SRC_NAME[src] || t.sub || "");
    if (t.cat && t.kind !== "aircraft") add("Category", t.cat);
    else if (t.cat && /^[AB][0-7]$/.test(t.cat)) add("Emitter category", t.cat);
    // geometric altitude, Mach, IAS and "seen by N stations" are not in the data the hub receives, so they are not shown
    const html = rows.map(([k, v, c]) => `<div class="${c}"><dt>${esc(k)}</dt><dd>${esc(v)}</dd></div>`).join("");
    if (f.rows._html !== html) { f.rows.innerHTML = html; f.rows._html = html; }
    if (t.extra) { f.extra.hidden = false; set("extra", t.extra); } else f.extra.hidden = true;
    const alertRow = rows.some((r) => r[2] === "alert");
    host.classList.toggle("emergency", alertRow);
  }

  const posMs = (t) => (Number.isFinite(t.age) && t.age > 0 ? t.t - t.age * 1000 : t.t);

  /** Refresh every field. Keep calls at <= 1 Hz. */
  function update(t) {
    if (!cur || t.id !== cur.id) return;
    cur = t;
    const now = Date.now();
    const m = t.meta || {};
    updateHeader(t, m);
    updateRoute(t, m, now);
    updateRows(t, m, now);
    const fam = iconShape(t);
    if (f.art.dataset.shape !== fam) renderStatic(t);
    const nm = m.type || (t.kind === "aircraft" ? FAMILY_NAME[fam] : cap(t.kind));
    set("picName", [nm, m.registration].filter(Boolean).join(" · "));
    maybeLoadPhoto(t);
  }

  function close() { host.hidden = true; host.classList.remove("open", "emergency"); cur = null; photoFor = ""; }

  return { open, update, close, syncToggles, current: () => cur };
}

function fmtLat(lat) { return Math.abs(lat).toFixed(4) + (lat >= 0 ? " N" : " S"); }
function fmtLon(lon) { return Math.abs(lon).toFixed(4) + (lon >= 0 ? " E" : " W"); }
function ageText(s) {
  if (!Number.isFinite(s) || s < 0) return "—";
  if (s < 90) return Math.round(s) + " s ago";
  if (s < 5400) return Math.round(s / 60) + " min ago";
  return Math.round(s / 3600) + " h ago";
}
