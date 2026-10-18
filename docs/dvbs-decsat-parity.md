# DVB-S/S2 in OnAir against DecSAT

[DecSAT](https://github.com/CasualArclamp/DecSAT) by Arclamp (CasualArclamp, GPL-3.0-or-later) is a satellite signal analyser written in Rust. The owner of OnAir asked
for everything it does that fits OnAir. This table lists every capability of DecSAT (its README, `docs/DESIGN.md`, `docs/STATUS.md`, its crates and apps), what OnAir had
at the start of this work, what was done, and what was left out and why. Status words: **done** (OnAir already had it), **added** (written in this branch), **partial**,
**planned** (to be added in this branch, in the order of the Chunk column), **skipped** (with the reason). Code taken from DecSAT keeps its notice and has a credits row
(THIRD_PARTY.md, CONTRIBUTORS.md); nothing encrypted is ever decoded, whatever DecSAT does.

Chunks: 1 GSE, IP, PCAP and the IP view; 2 MPE and ULE; 3 ACM, VCM, multistream, S2X superframes and GSE-HEM; 4 SI/PSI tables, DSM-CC carousels, data broadcast, TS payloads;
5 multicast media and the rest.

## Physical layer and demodulation

| DecSAT capability | OnAir | Chunk / reason |
|---|---|---|
| DVB-S2 PL demodulation, frame lock, MODCOD per frame | done (`core/src/dvbs`) | |
| Carrier recovery, Es/N0 (MER), Eb/N0 | done (MER, Es/N0); Eb/N0 partial | 5: show Eb/N0 |
| DVB-S (EN 300 421): code rate, rotation, inversion found blind; Viterbi, RS | done | |
| LDPC + BCH, all 21 S2 and 31 S2X codes, BBHEADER | done | |
| S2X PLS table, all 55 normal/short MODCODs, 2+4+2 8APSK to 256APSK | done | |
| S2X VL-SNR (medium FECFRAMEs, pi/2-BPSK, spread, 9 MODCODs) | done | |
| PL scrambling codes (Gold n found from pilots) | done (manual code + pilot hunt); S2X Table 19e preferred sequences | 3: check against Table 19e |
| Low roll-offs 0.15/0.10/0.05 signalled by RO alternating with 11 | done | |
| Blind symbol rate, roll-off, constellation, DVB-S2/S2X detection ("Identify") | done in the receiver's own hunt (spectrum, roll-off, pilots) | |
| Narrow carriers (down to about 10 kBd) | partial (the DVB-S/S2 receiver starts at about 1 Msym/s) | skipped: needs DecSAT's VFO front end (see below) |
| ACM/VCM: MODCOD per frame, VCM flag | done (telemetry), ACM terminal table added with GSE | 1, 3 |
| Multistream (ISI): one stream chosen; each ISI to its own stage | partial: a chosen ISI | 3: list the ISIs and decode them side by side |
| S2X superframing (Annex E): start sequence, format, formats 0 and 1 | missing | 3 |
| Modulator (DVB-S/S2/S2X, HackRF TX / IQ file) | done (`dvbs_tx.cpp`, TX framework) | 3: GSE and IP test signals |
| Waterfall, zoom, pan; carrier detection over a whole span | done (the app's spectrum views; the DVB-S/S2 mode takes one carrier) | |
| VFOs: many carriers decoded at once from one wide capture, survey of every carrier | skipped (partly done): DVB-T has multi-channel (`multitv`); a general VFO bank would be a new architecture for one mode | 5: reconsider after the rest |
| IQ file replay (cs8, cu8, cs16, cs24, cf32, WAV from SDR#/HDSDR/SDRuno/SDR++) | done (files with rate and format in the name or header) | |
| Live HackRF One 2-20 MS/s (pure Rust USB), LNB LO, input-level warning | done (HackRF over USB, LNB help, AGC) | |
| IQ recorder | done | |

## Transport stream and its tables

| DecSAT capability | OnAir | Chunk / reason |
|---|---|---|
| MPEG-TS out: file, UDP, TCP/HTTP | done (file, UDP, HLS, player) | |
| TS analyser: PIDs, services, now/next, network, tables, errors, bitrate per PID | done (`TsDemux`) | |
| PAT, PMT, SDT actual, NIT actual (name, LCN), EIT, TDT | done | |
| NIT transponder lists (frequency, polarisation, symbol rate, FEC, orbital position), other networks, BAT, SDT other, EIT other, TOT, CAT | missing | 4 |
| EPG for every service, schedule | done for present/following and the schedule of the actual TS | 4: other TS |
| Teletext, DVB subtitles | done | |
| In-app player (H.264, MPEG-2, HEVC; MP2/AAC/AC-3) | done | |
| BISS descrambling (DVB-CSA, user key) | skipped: this project never descrambles; payloads scrambled with CSA or any CA are reported as scrambled, nothing more | owner's rule |
| DSM-CC data and object carousels, file extraction | missing | 4 |
| Data broadcast signalling (AIT, MHEG-5, HbbTV) | missing | 4 |
| SCTE-35, EBU data, other TS payloads named | partial (PID labels) | 4 |
| Stuffing and null statistics | done | |

## IP data

| DecSAT capability | OnAir | Chunk / reason |
|---|---|---|
| GSE de-encapsulation (TS 102 606-1), fragments, CRC-32 | added | 1 (`dvbs_ip`) |
| GSE variants (length counting the header, split fragment id) chosen from the data; blind IPv4 search | added | 1 |
| Vendor CRC-32 trailer on the BBFRAME | added | 1 |
| IP validation, IPv4 fragment reassembly, flow statistics, protocols | added | 1 |
| PCAP export (link type RAW) | added | 1 |
| Flows whose UDP checksum fails marked as encrypted in transit; non-IP PDUs that read as random bytes marked as ciphertext | added; the data is never shown or decoded | 1 |
| ACM terminal table (GSE label, its IP addresses, the MODCODs it is sent in) | added | 1 |
| GSE-HEM, GSE-Lite (S2X) | missing | 3 |
| MPE (table 0x3E) to IP | missing | 2 |
| ULE (RFC 4326) to IP | missing | 2 |
| Multicast MPEG-TS in UDP/RTP found, analysed and played | missing | 5 |
| Multicast audio: SAP/SDP names, RTP AAC/Opus, now-playing | missing | 5 |
| DVB-RCS signalling (EN 301 790 / 545-2: NCR, TIM, SCT/FCT/TBTP) | missing | 3 or 5: public specs, the tables can be read from the sections of a stream |
| A hub's private burst plan / inroute signalling (Newtec D1h, worked out from a capture) | skipped: no public specification, DecSAT derived it from one recording; we have none to check against |
| Return pools and TDMA/MF-TDMA burst demodulation | skipped: needs the VFO front end and burst demodulators found from live recordings |

## Other protocols DecSAT decodes

| DecSAT capability | OnAir | Chunk / reason |
|---|---|---|
| Satellite beacons: carrier, drift, C/N0, PCM/PSK/PM telemetry, IRIG 106 and CCSDS frames, counter, major frame map, word plot, pointing tone | added earlier in this branch (TV sat beacon decoder: carrier, subcarrier, bi-phase, frames, counter, checksum, decoded view, export); CCSDS Reed-Solomon, K=7 and word plot missing | 5 |
| Carrier ID (DVB-CID, TS 103 129) | missing | 5 (public spec; DecSAT has no recording of one) |
| TPC 2964 (IESS-315), Comtech TPC 7/8, Q-Flex FastLink, K=7 Viterbi SCPC, IBS/IESS-309 with apt-X, E1 and G.728 voice, Comtech CDM modem types, radar feeds (ASTERIX, RCMS), IS-19 DAMA | skipped: professional VSAT and SCPC links with vendor framings, mostly worked out from live captures; they need DecSAT's blind carrier front end and recordings we do not have. Voice codecs would also be user-installed add-ons here |
| Generic PSK/APSK/QAM to symbols | skipped: a generic symbol dump, not part of the receivers here; the protocol lab (`core/src/protolab`) covers analysis of unknown signals |
| Survey of every carrier in a span | skipped with the VFO bank |

## Apps, tools and project

| DecSAT | OnAir | Chunk / reason |
|---|---|---|
| GUI (waterfall, VFOs, cards) | done (own GUI) | |
| CLI `decsat decode` / `modcods` | done (`dect2cli`) | 1: `--ip-pcap` |
| Signal gallery HTML | skipped: documentation of its own signals | |
| Opus and xaac (AAC SBR/PS) libraries | skipped: multicast audio needs patented codecs as user-installed add-ons, see docs/CODECS.md | |
| Tests (golden vectors from recordings, synthetic signals) | done in kind: synthetic and loopback tests per feature; golden vectors that do not need a recording | each chunk |
