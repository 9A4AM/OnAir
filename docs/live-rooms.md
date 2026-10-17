# Live rooms

Server > Go live streams a receiver's waterfall (and, if chosen, decoded ACARS text) to an audience on the web, with chat. Opt-in per session: nothing is sent before the consent window's Go live button, nothing is remembered between runs, nothing is stored on the server after the room ends.

Status: server in `web/rooms` (Cloudflare Worker + two Durable Objects, tested with `node --test`), host app in `app/rooms`. **Not deployed.** The app talks to `https://rooms.on-air.dev` (override with the `ONAIR_ROOMS_ORIGIN` environment variable for a test server).

## What is sent

| Item | Detail |
| --- | --- |
| Room title, mode, frequency | shown on the room page and the room list |
| Waterfall | one line a second, 512 bins, one byte per bin (level scaled to the waterfall's current range), max-pooled from the spectrum |
| Decoded text | ACARS text only, only when the box in the consent window is ticked; it is the same text that is already redacted for the map (phone numbers and e-mail addresses removed) and the server scrubs it again |
| Audio | **not sent**: the repo has no Opus encoder, and patented voice codecs are never bundled |
| Never | location, name, IP address of the host, audio |

## Architecture

* `POST /api/create` (needs `consent:true`) makes a room: signed host token (12 h), a host PIN, a view link `/r/<id>`. Limits: 3 rooms per IP per hour, 200 live rooms.
* `POST /api/r/<id>/push` every 2 s from the app (token in the JSON body, never in a URL): up to 4 waterfall lines and 20 texts. The Room object fans them out to viewers over hibernating WebSockets.
* Viewers: `/r/<id>` page, WebSocket `/api/r/<id>/ws`, at most 50 per room. First message `hello` gets a signed viewer token (kept in sessionStorage) so a ban follows the viewer across reconnects (the ban also stores a keyed hash of the IP, never the IP).
* Chat: one line per viewer per 2 s, 10 lines/s per room, 200 chars, links replaced by `[link]`, phone numbers and e-mails removed, a short profanity list starred out. Report button per line.
* Moderation: open the room page, "I am the host", type the PIN shown in the app (5 tries a minute); mute, kick and ban buttons appear next to chat lines and in the reports list.
* Expiry: the room ends 10 min after the last push, after 12 h, or when the host presses Stop. Ending closes all sockets and deletes all storage.
* Room list: `/api/rooms`, cached at the edge for 10 s, so list viewers do not reach the Lobby object more than ~6 times a minute in total.
* Security: HMAC-SHA256 signed tokens (`ROOM_SECRET`, a Worker secret), no secrets in URLs, strict input cleaning (control/bidi characters, lengths), pages use `textContent` only, CSP `default-src 'none'; script-src 'self'` and friends (`public/_headers`), WebSocket Origin check, binary frames are validated to exactly 512 bytes per line.

## Cost model

Per live room, one hour, with the app pushing every 2 s:

| Source | Count per hour | Billed as |
| --- | --- | --- |
| Host pushes (Worker + Room request) | 1800 | 2 requests each (Worker, DO) |
| Room alarm (expiry, viewer count, lobby heartbeat) | 120 + 60 lobby | 1 request each |
| Viewer joins | one per viewer | 1 request each |
| Viewer chat lines (incoming WebSocket messages) | at most 10/s, normally a few per minute | 1 request per 20 messages |
| Outgoing WebSocket messages (waterfall, texts, chat) | 30 per push-minute per viewer | not billed per message |
| Duration | about 450 GB-s (128 MB kept awake by the pushes) | duration |

Rule of thumb: about 4000 requests and 450 GB-s per room-hour, which at Cloudflare's paid rates (about $0.15 per million requests, $12.50 per million GB-s) is roughly **$0.006-0.007 per room-hour**; duration is about 90 % of it. 100 rooms live for 8 hours is about $5. Viewers add almost nothing: a viewer receives about 0.6 KB/s of waterfall (50 viewers: 30 KB/s per room) and Cloudflare does not charge for egress. Messages per minute per room: 30 pushes in, 1 waterfall frame out per push to each viewer (up to 1500 sends a minute at 50 viewers, unbilled), 2 alarms, 1 heartbeat.

The 50-viewer cap, the 200-room cap, the 3-rooms-per-IP rate limit and the idle expiry are the cost guards. The numbers are estimates from the plan's published prices, not measured on a deployed Worker.

## Deploy (when approved)

```
cd web/rooms
npm install
npx wrangler secret put ROOM_SECRET     # 32+ random bytes
# add a route or custom domain (rooms.on-air.dev) in wrangler.jsonc, then
npx wrangler deploy
```

## Tests

`cd web/rooms && npm test` (tokens, cleaning, moderation, rate limits, push parsing, Room and Lobby behaviour on a fake Durable Object state, Worker routes). Not covered: real WebSocket hibernation behaviour and the browser pages, which need a deployed or `wrangler dev` instance.
