# OnAir Discord bot

Cloudflare Worker `onair-discord`. Slash commands: `/map` (screenshot of https://map.on-air.dev plus stats) and `/stats` (stats only). No npm dependencies.

Stats come from the map Worker's `GET /live` (marker counts per layer, stations sending in the last 30 min, live WebSocket viewers, ingest posts per minute, server load %). Nothing was added to the map Worker.

## Setup

1. Go to https://discord.com/developers/applications, New Application.
2. On General Information copy the **Application ID** and **Public Key**.
3. Bot tab: Reset Token, copy the **bot token** (only used by the register script).
4. Invite link (no bot permissions needed):
   `https://discord.com/oauth2/authorize?client_id=1558882824717602895&scope=applications.commands`
5. Cloudflare dashboard, My Profile, API Tokens, Create Token, custom, permission **Account / Browser Rendering / Edit**. Copy the token, and your Account ID from the dashboard sidebar.
6. Secrets (from `web/discord`; paste each value when asked):
   ```
   npx wrangler secret put CF_ACCOUNT_ID
   npx wrangler secret put CF_API_TOKEN
   ```
   (`DISCORD_BOT_TOKEN` is only needed locally for step 8; it does not have to be a Worker secret.)
7. `npx wrangler deploy`. Note the `https://onair-discord.<subdomain>.workers.dev` URL.
8. Register the commands:
   ```
   DISCORD_APP_ID=... DISCORD_BOT_TOKEN=... node register-commands.mjs
   ```
9. In the Discord developer portal set **Interactions Endpoint URL** to the Worker URL and save (Discord sends a signed PING; it must pass).

Tests: `node --test test/verify.mjs`.

Behaviour: one screenshot per 20 s per Worker instance; the PNG is cached in memory and the Cache API and the footer says "cached N s ago". If the screenshot fails the stats embed is still sent with a note.

## Troubleshooting

- **Endpoint URL rejected / "invalid signature"**: the `DISCORD_PUBLIC_KEY` var in wrangler.jsonc is wrong or has whitespace; re-put it. Use the Public Key, not the client secret.
- **"The application did not respond"**: Worker not deployed, or commands registered against a different app ID.
- **Deferred message never completes**: Discord drops the interaction token after 15 minutes; check `wrangler tail` for "discord edit failed" (wrong `DISCORD_APP_ID`) or Browser Rendering errors.
- **Screenshot blank or tiles missing**: map tiles did not load in time; raise `waitForTimeout` in `src/screenshot.js`. The embed note shows the API error otherwise.
- **Screenshot unavailable 401/403**: token lacks Browser Rendering Edit, or wrong `CF_ACCOUNT_ID`.
- **Screenshot unavailable 429/402**: Browser Rendering needs the Workers Paid plan; free limits are small.
- **Commands not showing**: global commands can take a few minutes; re-run the register script.
