# Updating when GitHub is blocked or slow

OnAir normally asks `api.github.com` for the newest release and downloads the package from `github.com`. Where those are unreachable (for example in China) you can point OnAir at a **mirror**: a web address that forwards those two requests. This is **off by default** and nothing is sent to any mirror unless you turn it on. OnAir ships **no** built-in mirror: you enter one yourself (a friend, a community member, or your own).

## Settings (version button at the top right)

- **Update source**
  - **Automatic: GitHub**: GitHub only, unless you also tick **Use the mirror when GitHub cannot be reached**. Then the mirror is asked only after GitHub failed or timed out.
  - **GitHub only**: the mirror is never contacted.
  - **Mirror only**: only the mirror is contacted (GitHub never, so its checksum cannot be fetched; see below).
- **Mirror address**: `https://mirror.example/{url}` (`{url}` is replaced by the GitHub address; `{urlenc}` is the same, percent-encoded), or just a base address such as `https://mirror.example/gh` (the GitHub address is appended after a `/`). It must be https.

A mirror sees that you check for updates, your IP address and the version you run. OnAir only ever asks it for the project's own addresses (`api.github.com/repos/adswill/OnAir/releases...` and `github.com/adswill/OnAir/releases/...`), never anything else.

## How a package from a mirror is checked

1. Downloads are https only, with at most 5 redirects, a size limit (the size the release lists, plus a little), and timeouts (a stalled transfer is dropped).
2. The SHA-256 of the package must match. When the list of versions came through the mirror, OnAir asks **github.com itself** for the checksum (the digest in its API, else the release's `SHA256SUMS` file) and uses that. If the mirror's checksum differs from GitHub's, the update is refused.
3. If GitHub cannot be reached at all (or you chose Mirror only), only the mirror vouches for the package. OnAir says so in the update window and downloads only after you tick **I understand, download it anyway**. Such a package is never installed by itself: not at the start, not when you close OnAir. You press **Restart now and update**.

## The project's own mirror

OnAir's address field is pre-filled with `https://mirror.on-air.dev/{url}`, a Cloudflare Worker run by the maintainer (the same code as `web/mirror-worker/`). It forwards only the project's release list and release files, nothing else, and it is used only when you pick **Mirror only** or tick **Use the mirror when GitHub cannot be reached**. Its address is reachable through Cloudflare, which is often the case where github.com is not; if it is not for you, type another mirror's address.

## Running a mirror (Cloudflare Worker)

[`web/mirror-worker/worker.js`](../web/mirror-worker/worker.js) is a small example. It forwards only `GET` requests for `https://api.github.com/repos/adswill/OnAir/releases...` and `https://github.com/adswill/OnAir/releases/download/...`, with a strict allowlist, no cookies and no logging. Deploy it with Wrangler (`wrangler deploy` in that folder) to your own Cloudflare account, then give OnAir the address `https://<your-worker>.workers.dev/{url}`. Nothing is deployed by this repository, and a mirror run by someone else is only as trustworthy as they are: the checksum rules above limit what a bad mirror can do (it can refuse to answer, or log your requests, but it cannot swap the package when GitHub is reachable).
