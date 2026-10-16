# OnAir live map (map.on-air.dev)

Cloudflare Worker + static page. The Worker (`src/index.js`) holds the `MapHub` Durable Object (live tracks, WebSocket, `/live`, `/ingest`),
proxies OSM tiles and serves `/meta` (aircraft type, airline and route from adsbdb, with airport coordinates). The page is in `public/`.

## Files
- `src/index.js` - Worker + MapHub. `/meta` returns type, registration, airline, flight number and `route.origin/destination` (with lat/lon).
- `public/index.html`, `styles.css` - page shell and theme (dark tokens, drawer rail and bottom-sheet card on phones).
- `public/app.js` - map, layers, live feed, search (callsign / registration / hex / MMSI, `/` focuses it), stats bar, selection, `#hex=` deep links.
- `public/card.js` - the FlightRadar24-style info card (route, progress, estimated arrival, first seen / estimated departure, picture, data rows).
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

## Notes
- The aircraft photo is requested from the visitor's browser (Planespotters public API, no key) only after a click, with credit; it falls back to a generic illustration.
- ETA is estimated in the browser from great-circle distance and ground speed; it is hidden when the speed is unusable, the plane is on the ground or the destination position is unknown.
- "Departed about" is only shown when the aircraft was first seen (by this page) on the ground or below 3000 ft within 30 km of the departure airport.
- The hub does not carry geometric altitude, Mach/IAS or a "seen by N stations" count, so those rows are not shown.
