// VDL Mode 2 bit layer, HDLC and AVLC on top of the Rust crate rust/onair_vdl2 (DECT2_RUST_VDL2): the same functions as vdl2_proto.cpp, which keeps the C++
// under #ifndef DECT2_RUST_VDL2 together with the burst / HDLC / frame builders for the test signal and the ACARS reader. Empty when the crate is not built.
#ifdef DECT2_RUST_VDL2
#include "dect2/vdl2/vdl2_proto.h"
#include "dect2/rs/vdl2.h"
#include "vdl2_internal.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dect2 {

namespace {
struct AbiCheck {
    AbiCheck() {
        size_t s[3];
        onair_vdl2_struct_sizes(s);
        if (s[0] != sizeof(OnairVdl2Parsed) || s[1] != sizeof(OnairVdl2Layout) || s[2] != sizeof(OnairVdl2Addr)) {
            fprintf(stderr, "onair_vdl2: struct layout mismatch between C++ and Rust\n");
            abort();
        }
    }
} abiCheck;
template <class S> std::string str(const S& s) { return std::string((const char*)s.b, s.len); }
} // namespace

void vdl2Scramble(std::vector<uint8_t>& bits, size_t from) {
    if (from < bits.size()) onair_vdl2_scramble(bits.data() + from, bits.size() - from);
}

uint32_t vdl2HeaderWord(uint32_t lenBits) { return onair_vdl2_header_word(lenBits); }

bool vdl2HeaderDecode(uint32_t word, uint32_t& lenBits, bool& corrected) { return onair_vdl2_header_decode(word, &lenBits, &corrected); }

int vdl2LastFec(int n) { return onair_vdl2_last_fec(n); }

bool vdl2Layout(uint32_t lenBits, Vdl2Layout& l) {
    OnairVdl2Layout c;
    const bool ok = onair_vdl2_layout(lenBits, &c);
    l.dataOct = c.data_oct; l.fecOct = c.fec_oct; l.blocks = c.blocks; l.lastLen = c.last_len; l.lastFec = c.last_fec;
    return ok;
}

void vdl2RsEncode(const uint8_t data[kVdl2RsK], uint8_t parity[6]) { onair_vdl2_rs_encode(data, parity); }

int vdl2RsDecode(uint8_t cw[kVdl2RsN], int nParity) { return onair_vdl2_rs_decode(cw, nParity); }

bool vdl2BurstData(const uint8_t* bits, const Vdl2Layout& l, uint32_t lenBits, std::vector<uint8_t>& dataBits, int& rsFixed) {
    const OnairVdl2Layout c = {l.dataOct, l.fecOct, l.blocks, l.lastLen, l.lastFec};
    const int64_t nbits = 8 * ((int64_t)l.dataOct + l.fecOct);
    std::vector<uint8_t> out(lenBits);
    rsFixed = 0;
    const int64_t n = onair_vdl2_burst_data(bits, nbits < 0 ? 0 : (size_t)nbits, &c, lenBits, out.data(), out.size(), &rsFixed);
    if (n < 0) return false;
    out.resize((size_t)n);
    dataBits = std::move(out);
    return true;
}

uint16_t vdl2Fcs(const uint8_t* p, size_t n) { return onair_vdl2_fcs(p, n); }
bool vdl2FcsOk(const uint8_t* p, size_t n) { return onair_vdl2_fcs_ok(p, n); }

std::vector<std::vector<uint8_t>> vdl2HdlcFrames(const std::vector<uint8_t>& bits) {
    std::vector<uint8_t> out(bits.size() / 8 + 1);
    std::vector<uint32_t> lens(bits.size() / 16 + 1);
    size_t frames = 0, octets = 0;
    std::vector<std::vector<uint8_t>> res;
    if (!onair_vdl2_hdlc_frames(bits.data(), bits.size(), out.data(), out.size(), lens.data(), lens.size(), &frames, &octets)) return res;
    size_t at = 0;
    for (size_t i = 0; i < frames; i++) {
        res.emplace_back(out.begin() + (long)at, out.begin() + (long)(at + lens[i]));
        at += lens[i];
    }
    return res;
}

void vdl2PutAddr(uint8_t* o, const Vdl2Addr& a, bool last) {
    const OnairVdl2Addr c = {a.addr, a.type, (uint8_t)a.status};
    onair_vdl2_put_addr(&c, last, o);
}

Vdl2Addr vdl2GetAddr(const uint8_t* o) {
    OnairVdl2Addr c;
    onair_vdl2_get_addr(o, &c);
    Vdl2Addr a;
    a.addr = c.addr; a.type = c.type; a.status = c.status != 0;
    return a;
}

const char* vdl2AddrTypeName(int t) { return onair_vdl2_addr_type_name(t); }

bool vdl2ParseFrame(const uint8_t* f, size_t n, Vdl2Message& m) {
    auto p = std::make_unique<OnairVdl2Parsed>();
    if (!onair_vdl2_parse_frame(f, n, p.get())) return false;
    m.dst = p->dst; m.dstType = p->dst_type; m.src = p->src; m.srcType = p->src_type;
    m.onGround = p->on_ground != 0; m.response = p->response != 0;
    m.fromAir = p->from_air != 0; m.fromGround = p->from_ground != 0;
    m.frameKind = (char)p->frame_kind;
    m.frameName = str(p->frame_name);
    if (p->ns >= 0) m.ns = p->ns;
    if (p->nr >= 0) m.nr = p->nr;
    m.pf = p->pf != 0;
    if (p->proto.len) m.proto = str(p->proto);
    if (p->summary.len) m.summary = str(p->summary);
    m.detail = str(p->detail);
    if (p->has_pos) { m.hasPos = true; m.lat = p->lat; m.lon = p->lon; m.posKind = str(p->pos_kind); }
    if (p->has_alt) { m.hasAlt = true; m.altFt = p->alt_ft; }
    if (p->acars_at >= 0) vdl2ReadAcars(f + p->acars_at, n - (size_t)p->acars_at, m);
    return true;
}

} // namespace dect2
#endif
