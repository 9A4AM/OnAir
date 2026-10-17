# OnAir live map (map.on-air.dev)

Cloudflare Worker + static page. The Worker (`src/index.js`) holds the `MapHub` Durable Object (live tracks, WebSocket, `/live`, `/ingest`),
proxies OSM tiles and serves `/meta` (aircraft type, airline and route from adsbdb, with airport coordinates). The page is in `public/`.

## Files
- `src/index.js` - Worker + MapHub. `/meta` returns type, registration, airline, flight number and `route.origin/destination` (with lat/lon).
- `public/index.html`, `styles.css` - page shell and theme (dark tokens, drawer rail and bottom-sheet card on phones).
- `public/app.js` - map, layers, live feed, search (callsign / registration / hex / MMSI, `/` focuses it), stats bar, selection, `#hex=` deep links.
- `public/card.js` - the FlightRadar24-style info card (route, progress, estimated arrival, first seen / estimated departure, picture, data rows).
- `src/trail.js`, `src/trail-store.js` - pure trail rules (limits, thinning, request parsing) and the `TrailStore` that keeps them in the hub's SQLite (`trails` table).
- `public/trail.js` - merges the hub's trail with the points the page saw live.
- `public/geo.js` - pure maths: distance, ETA, progress, departure estimate, squawk meanings, formatting.
- `public/types.js` - pure mapping from ICAO type / ADS-B category / AIS type to an icon shape.
- `public/shapes.js`, `icons.js` - icon geometry (SVG paths) and lazy canvas rendering at device resolution.
- `public/airports.js` - ~290 major airports (fallback coordinates when the route lookup has none).
- `public/demo.js` - `?demo=1` synthetic data.
- `test/` - `node --test` tests for the pure modules.

## Preview offline (demo)
```
cd web/map/public && python3 -m http.server 8765
# open http://localhost:8765/?demo=1
```
The demo has ~60 synthetic aircraft, ships, sondes, APRS stations and satellites, clearly marked DEMO. Tiles load from openstreetmap.org when not on on-air.dev.

## Test
```
cd web/map && node --test test/
```

## Deploy
The owner runs `npx wrangler deploy` in `web/map`. Nothing here deploys automatically.

## Trails (kept by the hub, so a refresh keeps them)
- Each accepted fix with a position is stored in the `trails` table (created with `CREATE TABLE IF NOT EXISTS` inside the Durable Object: no wrangler migration needed). Rows: time, lat, lon, alt, heading.
- Thinning: a point is kept only if >= 5 s and >= 100 m after the last kept one, or >= 5 s after it with a heading change of 8 degrees or more (keeps corners).
- Limits per track: aircraft / FLARM / drones 60 min or 300 points; ships 6 h / 200; sondes 4 h / 1000 (a whole flight); APRS, amateur, DSC 6 h / 100; buoys 6 h / 50; satellites and stations none. Trails go when their track expires; the whole table is capped at 150000 rows (oldest first).
- Writes happen in the same synchronous run as the track upsert (one storage transaction); the last kept point per track is cached in memory and re-read after a restart or prune.
- `GET /trail?id=<track id>` or `GET /trails?ids=a,b,c` (max 60) -> `{ now, trails: { id: [[t_ms, lat, lon, alt, heading], ...] } }`, oldest first.
- `GET /live?trails=1` adds `trail` (same points) to each track that has one. Plain `/live` is unchanged, so the Android app, the Discord bot and the desktop app are not affected.
- The page fetches `/trail` when a track is selected (also from a `#hex=` link after a refresh) or the Track toggle is turned on, and merges it with fixes seen live (deduped by timestamp). The Track toggle is remembered in localStorage.

## Notes
- The aircraft photo is requested from the visitor's browser (Planespotters public API, no key) only after a click, with credit; it falls back to a generic illustration.
- ETA is estimated in the browser from great-circle distance and ground speed; it is hidden when the speed is unusable, the plane is on the ground or the destination position is unknown.
- "Departed about" is only shown when the aircraft was first seen (by this page) on the ground or below 3000 ft within 30 km of the departure airport.
- The hub does not carry geometric altitude, Mach/IAS or a "seen by N stations" count, so those rows are not shown.
