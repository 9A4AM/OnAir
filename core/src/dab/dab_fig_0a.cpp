// DAB FIG 0 decoders, part 1 (ensemble, sub-channels, services, components, time, ...). Ported from etisnoop (GPL-3.0-or-later).
#include "dab_fig_internal.h"

namespace dect2 {

namespace dab_fig_detail {

// ------------------------------------------------------------------ FIG 0
void fig0_0(Fig& g) {   // EN 300 401 6.4
    const uint8_t* f = g.f;
    g.add(0, pf("Ensemble ID=0x%04X", g.u16(1)));
    g.add(0, pf("Country ID=%d", f[1] >> 4));
    g.add(0, pf("Ensemble reference=%d", (f[1] & 0x0F) * 256 + f[2]));
    const int ch = f[3] >> 6;
    g.add(0, pf("Change flag=%d", ch));
    g.add(0, pf("Alarm flag=%d", (f[3] >> 5) & 1));
    g.add(0, pf("CIF Count=%d/%d", f[3] & 0x1F, f[4]));
    if (ch) g.add(0, pf("Occurrence change=%d", f[5]));
}

void fig0_1(Fig& g) {   // 6.2.1
    const uint8_t* f = g.f;
    for (int i = 1; i <= g.len - 3;) {
        const int id = f[i] >> 2, start = ((f[i] & 3) << 8) | f[i + 1];
        g.add(0, pf("Sub-channel 0x%02X", id));
        g.add(1, pf("Subch=0x%x", id));
        g.add(1, pf("start_addr=%d", start));
        if (f[i + 2] & 0x80) {
            const int opt = (f[i + 2] >> 4) & 7, lvl = (f[i + 2] >> 2) & 3, size = ((f[i + 2] & 3) << 8) | f[i + 3];
            g.add(1, "form=long");
            if (opt == 0) g.add(1, pf("EEP=%d-A", lvl + 1));
            else if (opt == 1) g.add(1, pf("EEP=%d-B", lvl + 1));
            else g.err(pf("Invalid option %d protection %d", opt, lvl));
            g.add(1, pf("subch size=%d", size));
            i += 4;
        } else {
            const int sw = (f[i + 2] >> 6) & 1;
            g.add(1, "form=short");
            if (sw) g.err(pf("Invalid table_switch %d", sw));
            g.add(1, pf("table index=%d", f[i + 2] & 0x3F));
            i += 3;
        }
    }
}

void fig0_2(Fig& g) {   // 6.3.1
    const uint8_t* f = g.f;
    for (int k = 1; k < g.len;) {
        uint32_t sid;
        int ecc = 0, cid, sref;
        if (!g.pd) { sid = g.u16(k); cid = f[k] >> 4; sref = (f[k] & 0x0F) * 256 + f[k + 1]; k += 2; }
        else { sid = g.u32(k); ecc = f[k]; cid = f[k + 1] >> 4; sref = (f[k + 1] & 0x0F) * 65536 + f[k + 2] * 256 + f[k + 3]; k += 4; }
        const int local = f[k] >> 7, caid = (f[k] >> 4) & 7, ncomp = f[k] & 15;
        g.add(0, pf("Service 0x%X", sid));
        g.add(1, pf("Service ID=0x%X", sid));
        if (g.pd) g.add(1, pf("ECC=%d", ecc));
        g.add(1, pf("Country id=%d", cid));
        g.add(1, pf("Service reference=%d", sref));
        g.add(1, pf("Number of components=%d", ncomp));
        g.add(1, pf("Local flag=%d", local));
        g.add(1, pf("CAID=%d", caid));
        k++;
        g.add(1, "Components:");
        for (int i = 0; i < ncomp; i++, k += 2) {
            const int timd = f[k] >> 6, ps = (f[k + 1] >> 1) & 1, ca = f[k + 1] & 1, scty = f[k] & 0x3F, sub = f[k + 1] >> 2;
            g.add(2, pf("Component %d", i));
            g.add(3, pf("ID=%d", i));
            g.add(3, ps ? "primary=false" : "primary=true");
            if (timd == 0) {
                g.add(3, "Mode=audio stream");
                const char* n = scty == 0 ? "MPEG Foreground sound" : scty == 1 ? "MPEG Background sound" : scty == 2 ? "Multi Channel sound" :
                                scty == 63 ? "AAC sound" : "Unknown ASCTy";
                g.add(3, pf("ASCTy=%s (%d)", n, scty));
                g.add(3, pf("SubChannel ID=0x%02X", sub));
            } else if (timd == 1) {
                g.add(3, "Mode=data stream");
                g.add(3, pf("DSCTy=%d %s", scty, dscty(scty)));
                g.add(3, pf("SubChannel ID=0x%02X", sub));
            } else if (timd == 2) {
                g.add(3, "Mode=FIDC");
                g.add(3, pf("DSCTy=%d %s", scty, dscty(scty)));
                g.add(3, pf("Fast Information Data Channel ID=0x%02X", sub));
            } else {
                g.add(3, "Mode=MSC Packet");
                g.add(3, pf("SubChannel ID=0x%02X", sub));
            }
            g.add(3, pf("CA=%d", ca));
        }
    }
}

void fig0_3(Fig& g) {   // 6.3.2
    const uint8_t* f = g.f;
    for (int i = 1; i < g.len - 4;) {
        const int scid = (f[i] << 4) | (f[i + 1] >> 4), rfa = (f[i + 1] >> 1) & 7, caorg = f[i + 1] & 1;
        const int dg = (f[i + 2] >> 7) & 1, rfu = (f[i + 2] >> 6) & 1, ty = f[i + 2] & 0x3F;
        g.add(0, pf("Packet component SCId 0x%X", scid));
        g.add(1, pf("SCId=0x%X", scid));
        g.add(1, pf("CAOrg flag=%d CAOrg field %s", caorg, caorg ? "present" : "absent"));
        g.add(1, pf("DG flag=%d", dg));
        g.add(1, pf("DSCTy=%d %s", ty, dscty(ty)));
        g.add(1, pf("SubChId=0x%X", f[i + 3] >> 2));
        g.add(1, pf("Packet address=0x%X", ((f[i + 3] & 3) << 8) | f[i + 4]));
        if (rfa) g.err(pf("Rfa=%d invalid value", rfa));
        if (rfu) g.err(pf("Rfu=%d invalid value", rfu));
        i += 5;
        if (caorg) {
            if (i < g.len - 1) g.add(1, pf("CAOrg=0x%X CAMode=%d \"%s\" SharedFlag=0x%X%s", g.u16(i), f[i] >> 5, kCaMode[f[i] >> 5], f[i + 1], f[i + 1] ? "" : " invalid"));
            else g.err("Invalid figlen");
            i += 2;
        }
    }
}

void fig0_5(Fig& g) {   // 8.1.2
    const uint8_t* f = g.f;
    for (int i = 1; i < g.len - 1;) {
        if (!(f[i] >> 7)) {
            const int fic = (f[i] >> 6) & 1;
            g.add(0, pf("Component language %s 0x%X", fic ? "FIDCId" : "SubChId", f[i] & 0x3F));
            g.add(1, "form=short");
            g.add(1, pf("MSC/FIC flag=%d %s", fic, fic ? "FIC" : "MSC"));
            g.add(1, pf(fic ? "FIDCId=0x%X" : "SubChId=0x%X", f[i] & 0x3F));
            g.add(1, pf("Language=0x%X %s", f[i + 1], language(f[i + 1])));
            i += 2;
        } else {
            g.add(0, "Component language, long form");
            g.add(1, "form=long");
            if (i < g.len - 2) {
                if ((f[i] >> 4) & 7) g.err(pf("Rfa=%d invalid value", (f[i] >> 4) & 7));
                g.add(1, pf("SCId=0x%X", ((f[i] & 0x0F) << 8) | f[i + 1]));
                g.add(1, pf("Language=0x%X %s", f[i + 2], language(f[i + 2])));
            } else g.err("Long form FIG is too short");
            i += 3;
        }
    }
}

void fig0_6(Fig& g) {   // 8.1.15
    const uint8_t* f = g.f;
    for (int i = 1; i < g.len - 1;) {
        const int idl = (f[i] >> 7) & 1, la = (f[i] >> 6) & 1, sh = (f[i] >> 5) & 1, ils = (f[i] >> 4) & 1, lsn = ((f[i] & 0x0F) << 8) | f[i + 1];
        const int key = (g.oe << 15) | (g.pd << 14) | (sh << 13) | (ils << 12) | lsn;
        g.add(0, pf("Link LSN %d", lsn));
        g.add(1, pf("Id list flag=%d", idl));
        g.add(1, pf("LA=%d %s", la, la ? "active" : "inactive"));
        g.add(1, pf("S/H=%d %s", sh, sh ? "Hard" : "Soft"));
        g.add(1, pf("ILS=%d %s", ils, ils ? "international" : "national"));
        g.add(1, pf("LSN=%d", lsn));
        g.add(1, pf("database key=0x%04x", key));
        i += 2;
        if (!idl) {
            if (g.cn == 0) g.add(1, "CEI=true");
            continue;
        }
        if (i >= g.len) continue;
        const int n = f[i] & 15;
        if (!g.pd) {
            const int idlq = (f[i] >> 5) & 3, shd = (f[i] >> 4) & 1;
            g.add(1, pf("IdLQ=%d", idlq));
            g.add(1, pf("Shd=%d %s", shd, shd ? "b11-8 in 4-F are different services" : "single service"));
            g.add(1, "Id List:");
            const int w = ils ? 3 : 2;
            for (int j = 0; j < n && i + 1 + w * j + (w - 1) < g.len; j++) {
                const int o = i + 1 + w * j;
                const bool first = (j == 0 && g.oe == 0 && g.cn == 0) || idlq == 0;
                const char* kind = first ? "DAB SId" : idlq == 1 ? "RDS PI" : idlq == 2 ? "(AM-FM legacy)" : "DRM-AMSS service";
                g.add(2, "-");
                if (!ils) g.add(3, pf("%s=0x%X", kind, g.u16(o)));
                else g.add(3, pf("%s=ecc 0x%02X Id 0x%04X", kind, f[o], g.u16(o + 1)));
            }
            if (n == 0 && idlq == 1) g.err("deadlink");
            i += n * w + 1;
        } else {
            g.add(1, "Id List:");
            for (int j = 0; j < n && i + 4 + 4 * j < g.len; j++) g.add(2, pf("- 0x%X", g.u32(i + 1 + 4 * j)));
            i += n * 4 + 1;
        }
    }
}

void fig0_7(Fig& g) {   // 6.4.2
    if (g.len != 3) { g.err("FIG0/7 has incorrect length"); return; }
    const unsigned v = g.u16(1);
    g.add(0, pf("Services=%u", v >> 10));
    g.add(0, pf("Count=%u", v & 0x3FF));
}

void fig0_8(Fig& g) {   // 6.3.5
    const uint8_t* f = g.f;
    for (int i = 1; i < g.len - (2 + 2 * g.pd);) {
        uint32_t sid;
        if (!g.pd) { sid = g.u16(i); i += 2; } else { sid = g.u32(i); i += 4; }
        const int ext = f[i] >> 7, rfa = (f[i] >> 4) & 7, scids = f[i] & 15;
        g.add(0, pf("Component SId 0x%X SCIdS %d", sid, scids));
        g.add(1, pf("SId=0x%X", sid));
        g.add(1, pf("Ext flag=%d 8-bit Rfa %s", ext, ext ? "present" : "absent"));
        if (rfa) g.err(pf("Rfa=%d invalid value", rfa));
        g.add(1, pf("SCIdS=0x%X", scids));
        i++;
        if (i >= g.len) break;
        const int ls = f[i] >> 7;
        g.add(1, pf("L/S flag=%d %s", ls, ls ? "Long form" : "Short form"));
        if (!ls) {
            if (i < g.len - ext) {
                const int fic = (f[i] >> 6) & 1;
                if (!fic) g.add(1, pf("MSC/FIC flag=0 MSC, SubChId=0x%X", f[i] & 0x3F));
                else g.add(1, pf("MSC/FIC flag=1 FIC, FIDCId=0x%X", f[i] & 0x3F));
                if (ext && f[i + 1]) g.err(pf("Rfa=0x%X invalid value", f[i + 1]));
            }
            i += 1 + ext;
        } else {
            if (i < g.len - 1) {
                if ((f[i] >> 4) & 7) g.err(pf("Rfa=%d invalid value", (f[i] >> 4) & 7));
                g.add(1, pf("SCId=0x%X", ((f[i] & 0x0F) << 8) | f[i + 1]));
            }
            i += 2;
        }
    }
}

void fig0_9(Fig& g) {   // 8.1.3.2
    const uint8_t* f = g.f;
    int i = 1;
    if (i >= g.len - 2) return;
    const int ext = f[i] >> 7, uniq = (f[i] >> 6) & 1;
    int lto = f[i] & 0x3F;
    g.add(0, pf("Ext flag=%d extended field %s", ext, ext ? "present" : "absent"));
    g.add(0, pf("LTO uniq=%d %s", uniq, uniq ? "several time zones" : "one time zone (time specified by Ensemble LTO)"));
    g.add(0, pf("Ensemble LTO=0x%X UTC%s", lto, ltoText(lto).c_str()));
    if (lto & 0x20) lto |= ~0x3F;
    if (std::abs(lto) > 24) g.err("LTO out of range -12 hours to +12 hours");
    g.add(0, pf("Ensemble ECC=0x%X", f[i + 1]));
    g.add(0, pf("International Table Id=0x%X", f[i + 2]));
    g.add(0, pf("database key=0x%x", (g.oe << 1) | g.pd));
    i += 3;
    if (!ext) return;
    g.add(0, "Subfields:");
    while (i < g.len) {
        const int n = f[i] >> 6, l = f[i] & 0x3F;
        g.add(1, "Subfield");
        g.add(2, pf("Number of services=%d", n));
        g.add(2, pf("LTO=0x%X UTC%s", l, ltoText(l).c_str()));
        int sl = l;
        if (sl & 0x20) sl |= ~0x3F;
        if (std::abs(sl) > 24) g.err("LTO in extended field out of range -12 hours to +12 hours");
        if (n == 0 && l == 0) g.add(2, "CEI=true");
        i++;
        if (!g.pd) {
            if (i < g.len) {
                g.add(2, pf("ECC=0x%X", f[i]));
                i++;
                for (int j = i; j < i + n * 2 && j < g.len; j += 2) g.add(2, pf("SId=0x%X", g.u16(j)));
                i += n * 2;
            }
        } else {
            for (int j = i; j < i + n * 4 && j < g.len; j += 4) g.add(2, pf("SId=0x%X", g.u32(j)));
            i += n * 4;
        }
    }
}

void fig0_10(Fig& g) {   // 8.1.3.1
    const uint8_t* f = g.f;
    const uint32_t mjd = ((uint32_t)(f[1] & 0x7F) << 10) | ((uint32_t)f[2] << 2) | (f[3] >> 6);
    const bool utc = f[3] & 8;
    const int hh = ((f[3] & 7) << 2) | (f[4] >> 6), mm = f[4] & 0x3F;
    g.add(0, utc ? "form=long" : "form=short");
    g.add(0, pf("MJD=0x%X %s", mjd, mjdDate((int)mjd).c_str()));
    g.add(0, pf("LSI=%u", (f[3] >> 5) & 1));
    g.add(0, pf("ConfInd=%u", (f[3] >> 4) & 1));
    if (utc) g.add(0, pf("UTC Time=%02d:%02d:%02d.%d", hh, mm, f[5] >> 2, ((f[5] & 3) << 8) | f[6]));
    else g.add(0, pf("UTC Time=%02d:%02d", hh, mm));
}

void mainIdCheck(Fig& g, int mainId) {
    const int mode = g.ctx->mode;
    if ((mode == 1 || mode == 2 || mode == 4) && mainId > 69) g.err(pf("invalid value for transmission mode %d", mode));
    else if (mode == 3 && mainId > 5) g.err(pf("invalid value for transmission mode %d", mode));
}

void fig0_11(Fig& g) {   // 8.1.16.1
    const uint8_t* f = g.f;
    for (int i = 1; i < g.len - 1;) {
        const int gaty = f[i] >> 4, ge = (f[i] >> 3) & 1, region = ((f[i] & 7) << 8) | f[i + 1];
        const int key = (g.oe << 12) | (g.pd << 11) | region;
        i += 2;
        g.add(0, pf("Region 0x%X", region));
        g.add(1, pf("GATy=%d", gaty));
        g.add(1, gaty == 0 ? "Geographical area=defined by TII list" :
                 gaty == 1 ? "Geographical area=defined as a spherical rectangle by the co-ordinates of one corner and its extents" :
                             "Geographical area=reserved for future use");
        g.add(1, pf("G/E flag=%d %s coverage area", ge, ge ? "Global" : "Ensemble"));
        g.add(1, pf("RegionId=0x%X", region));
        g.add(1, pf("database key=0x%X", key));
        if (gaty == 0) {
            if (i >= g.len) continue;
            if (f[i] >> 5) g.err(pf("Rfu=%d invalid value", f[i] >> 5));
            const int ntii = f[i] & 0x1F;
            g.add(1, pf("Length of TII list=%d", ntii));
            if (!ntii) g.add(1, "CEI=true");
            i++;
            int j = 0;
            for (; i < g.len - 1 && j < ntii; j++) {
                const int main = f[i] & 0x7F;
                if (f[i] >> 7) g.err(pf("Rfa=%d invalid value, MainId=0x%X", f[i] >> 7, main));
                else g.add(1, pf("MainId=0x%X", main));
                mainIdCheck(g, main);
                if (f[i + 1] >> 5) g.err(pf("Rfa=%d invalid value", f[i + 1] >> 5));
                const int nsub = f[i + 1] & 0x1F;
                g.add(1, pf("Length of SubId=%d", nsub));
                i += 2;
                int bit = 3, sub = 0, k = 0;
                for (; i < g.len && k < nsub; k++) {
                    if (bit >= 0) {
                        sub |= (f[i] >> bit) & 0x1F;
                        g.add(2, pf("SubId=0x%X", sub));
                        if (sub == 0 || sub > 23) g.err(pf("Invalid SubId=0x%X", sub));
                        bit -= 5;
                        sub = 0;
                    }
                    if (bit < 0) { sub = (f[i] << std::abs(bit)) & 0x1F; bit += 8; i++; }
                }
                if (bit > 3) i++;
                if (k < nsub) g.err(pf("%d SubId missing, fig length too short !", nsub - k));
            }
            if (j < ntii) g.err(pf("%d Transmitter group missing, fig length too short !", ntii - j));
        } else if (gaty == 1) {
            if (i < g.len - 6) {
                const int16_t la = (int16_t)g.u16(i), lo = (int16_t)g.u16(i + 2);
                const double lat = la * 90.0 / 32768, lng = lo * 180.0 / 32768;
                g.add(1, pf("Lat Lng coarse=0x%X 0x%X => %f, %f", (unsigned)(uint16_t)la, (unsigned)(uint16_t)lo, lat, lng));
                const int elat = (f[i + 4] << 4) | (f[i + 5] >> 4), elng = ((f[i + 5] & 0x0F) << 8) | f[i + 6];
                g.add(1, pf("Extent Lat Lng=0x%X 0x%X => %f, %f", elat, elng, lat + elat * 90.0 / 32768, lng + elng * 180.0 / 32768));
            } else g.err("Coordinates missing, fig length too short !");
            i += 7;
        } else {
            g.add(1, pf("stop Region definition iteration %d/%d", i, g.len));
            g.err("Stopping iteration because Rfu encountered");
            break;
        }
    }
}

void fig0_13(Fig& g) {   // 8.1.20
    const uint8_t* f = g.f;
    int k = 1;
    uint32_t sid;
    if (!g.pd) { sid = g.u16(k); k += 2; } else { sid = g.u32(k); k += 4; }
    const int scids = f[k] >> 4, no = f[k] & 15;
    k++;
    g.add(0, pf("SId=0x%X", sid));
    g.add(0, pf("SCIdS=%u", scids));
    g.add(0, pf("User applications(%d):", no));
    for (int n = 0; n < no && k + 2 <= g.len; n++) {
        const int type = (f[k] << 3) | (f[k + 1] >> 5), ulen = f[k + 1] & 0x1F;
        k += 2;
        g.add(1, pf("User application %d", n));
        g.add(2, pf("User Application=%d '%s'", type, userApp(type)));
        g.add(2, pf("length=%u", ulen));
        if (ulen < 2) { k += ulen; continue; }
        int eff = ulen;
        if (!g.pd) {   // programme services carry the X-PAD data field first
            const int caorg = (f[k] >> 6) & 1;
            g.add(2, pf("CAflag=%d", (f[k] >> 7) & 1));
            g.add(2, pf("CAOrgflag=%d", caorg));
            g.add(2, pf("XPAD_AppTy=%d", f[k] & 0x1F));
            g.add(2, pf("DGflag=%d", (f[k + 1] >> 7) & 1));
            g.add(2, pf("DSCTy=%d", f[k + 1] & 0x3F));
            k += 2;
            eff -= 2;
            if (caorg) {
                if (ulen < 4) g.err("User Application Data Length too short to contain CAOrg!");
                else { g.add(2, pf("ca_org=%u", g.u16(k))); k += 2; eff -= 2; }
            }
        }
        std::string ua = "UA Data=[";
        for (int i = 0; i < eff && k + i < g.len; i++) ua += pf(i ? ", 0x%02x" : "0x%02x", f[k + i]);
        g.add(2, ua + "]");
        k += eff;
    }
}

void fig0_14(Fig& g) {   // 6.2.2
    static const char* const fec[4] = {"no FEC scheme applied", "FEC scheme applied according to ETSI EN 300 401 clause 5.3.5",
        "reserved for future definition", "reserved for future definition"};
    for (int i = 1; i < g.len; i++) {
        g.add(0, pf("Sub-channel 0x%X", g.f[i] >> 2));
        g.add(1, pf("SubChId=0x%X", g.f[i] >> 2));
        g.add(1, pf("FEC scheme=%d %s", g.f[i] & 3, fec[g.f[i] & 3]));
    }
}


} // namespace dab_fig_detail

} // namespace dect2
