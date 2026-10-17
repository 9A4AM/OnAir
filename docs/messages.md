# Messages: the decoded-message database

View > Messages opens one searchable store of everything the decoders heard: ADS-B, ACARS, VDL2, HFDL, pagers, AIS, APRS, radiosondes, LoRa, BLE, drone ID and DSC.
It is a SQLite file, `<data folder>/messages/messages.db`, with full-text search (words are ANDed, `word*` is a prefix), filters for mode, identity, time range and
"with position", and a timeline (newest first, 300 per page).

- Export: CSV, JSON (with every decoded field) or ADIF (APRS stations with a call sign) of everything the current filters match.
- Retention: Storage section. Most rows, size cap in MB and days to keep; the oldest rows go first. Default 1 million rows, 512 MB, no age limit. "Record decoded
  messages" switches the feed off. "Clear all" empties the file.
- Chatty updates (aircraft, vessel, sonde, BLE and drone positions) are stored at most once a minute per object.
- Voice audio and encrypted traffic are never stored; the store refuses them.
- The feed is a tap on the Automations event bus (`Engine::setTap`), so a decoder needs no extra code beyond emitting its automation event.
