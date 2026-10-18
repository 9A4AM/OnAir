# DVB-S, DVB-S2 and DVB-S2X in OnAir

The receiver takes the L-band IF of an LNB and finds the carrier, symbol rate, roll-off, modulation and code rate by itself (DVB-S: EN 300 421; DVB-S2: EN 302 307-1;
DVB-S2X: EN 302 307-2, including the VL-SNR frames). See `docs/dvbs-decsat-parity.md` for what was added from the study of DecSAT and what was left out.

## IP data (GSE)

When the BBFRAMEs of a DVB-S2 signal carry a generic stream instead of a transport stream (an IP data service, a VSAT outbound), the receiver de-encapsulates **GSE**
(ETSI TS 102 606-1): packets, fragments reassembled across BBFRAMEs, CRC-32 checked. Four readings of GSE are tried on every data field (the standard, GSE_LENGTH counting its
own header, a split 6-bit fragment id and 2-bit counter, and both) and the one that yields valid IP packets is used; a vendor's CRC-32 at the end of the BBFRAME is checked and
taken off. When no reading produces IP, the data fields are searched blind for IPv4 packets. IP packets are validated strictly (version, lengths, header checksum), IPv4
fragments are put together, and the packets are counted.

The receiver page shows, under IP DATA: the source, packets, bytes, IPv4 and IPv6, the protocols, the destination ports, the biggest flows (source, destination, protocol,
packets, bytes), and for an ACM outbound a table of **GSE labels** (a terminal's address): its PDUs and bytes, the IP addresses sent to it and the MODCODs it is sent in, which is
the terminal's link as the hub sees it. **Write a PCAP file** writes every IP packet to `dvbs_ip.pcap` in the data folder (libpcap, link type RAW), also with the window hidden.
From the command line, `dect2cli --standard dvbs --ip-pcap file.pcap`.

**Privacy and encryption.** Satellite IP links can carry other people's private traffic; the user is responsible for what local law allows them to receive and keep. Nothing
encrypted or scrambled is decoded, and payloads are never shown: the view holds addresses, ports, protocols and counts. A flow whose headers are good but whose UDP checksums
fail was changed after the sender made them (an inline encryptor) and is marked encrypted; GSE PDUs that are not IP and read as random bytes (8 bits per byte over 32 KiB) are
called ciphertext. The PCAP file holds the packets as received (what is encrypted stays encrypted).

## IP in the transport stream (MPE and ULE)

The packets of the transport stream that the receiver delivers are also searched for IP: **MPE** (EN 301 192: sections with table_id 0x3E on any PID that carries one; a PES start
rules a PID out; the datagram is taken also from behind LLC/SNAP; sections whose payload is scrambled are skipped, never decoded) and **ULE** (RFC 4326: SNDUs in the TS payload, CRC-32
checked, bridged Ethernet frames and IPv6 as well; a PID is accepted after three valid SNDUs that carry IP, so video and audio PIDs are never taken for it). Their packets go to the same
statistics and PCAP file as GSE; the IP DATA source line says GSE, MPE, ULE or a mix. MPE is ported from DecSAT (`decsat-ts/mpe.rs`); ULE is written from the RFC.

## Credits

The GSE, IP and MPE code is adapted from DecSAT by Arclamp (CasualArclamp, GPL-3.0-or-later; https://github.com/CasualArclamp/DecSAT), crates `decsat-gse` and `decsat-ip`, rewritten in
C++; its notes on what was seen on real links are kept in the comments. DecSAT credits dontlookup (MIT) for the two GSE variants. See THIRD_PARTY.md.

## Tests

`tests/dvbs/test_dvbs_ip.cpp`: GSE round trips for every variant with fragmentation, the variant picked from the data, a link without IP, ciphertext recognised, a vendor CRC trailer,
a lost fragment, bridged Ethernet, the blind search, IP fragments in any order, flow statistics, encrypted flows and the PCAP file. `tests/dvbs/test_dvbs_gse.cpp`: the whole chain
(DVB-S2 QPSK 3/5 with 8-bit samples and noise, UDP in GSE with fragmented PDUs and three terminals), for two GSE variants, ending in a PCAP file that holds exactly the packets that were sent.
