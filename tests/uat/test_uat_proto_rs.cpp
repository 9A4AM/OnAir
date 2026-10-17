// Differential test: dump978's C decoder (uat_decode.c, a generated copy with the functions renamed ref_*, see CMakeLists.txt) against the Rust one
// (uat_decode_rs.cpp over rust/onair_uat, DECT2_RUST_UAT). The same frames go to both and every field has to be identical (doubles bit for bit): the real
// DO-282 vectors, then ONAIR_DIFF_ITERS (default 200000) random / structured / mutated downlink payloads of every message type and the same number of
// uplink frames (random application data, valid information frame chains with FIS-B headers of all four time formats, truncated / overrun chains), uplink
// decodes also into a pre-filled (dirty) struct to prove the same fields are written.
#include "dect2/uat/uat_decode_c.h"
#include "dect2/uat/uat_frame.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
extern "C" {
void ref_uat_decode_adsb_mdb(uint8_t* frame, struct uat_adsb_mdb* mdb);
void ref_uat_decode_uplink_mdb(uint8_t* frame, struct uat_uplink_mdb* mdb);
}

static long fails = 0, checks = 0;
#define SAME(c, ...) do { checks++; if (!(c)) { if (fails++ < 20) { printf("MISMATCH: " __VA_ARGS__); printf("\n"); } } } while (0)
static std::mt19937 g(24681);
static bool dEq(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }

static bool adsbEq(const uat_adsb_mdb& a, const uat_adsb_mdb& b) {
#define F(x) (a.x == b.x)
    return F(has_sv) && F(has_ms) && F(has_auxsv) && F(position_valid) && F(ns_vel_valid) && F(ew_vel_valid) && F(speed_valid) && F(dimensions_valid) && F(mdb_type) &&
           F(address_qualifier) && F(address) && dEq(a.lat, b.lat) && dEq(a.lon, b.lon) && F(altitude_type) && F(altitude) && F(nic) && F(airground_state) && F(ns_vel) &&
           F(ew_vel) && F(track_type) && F(track) && F(speed) && F(vert_rate_source) && F(vert_rate) && dEq(a.length, b.length) && dEq(a.width, b.width) && F(position_offset) &&
           F(utc_coupled) && F(tisb_site_id) && F(emitter_category) && F(callsign_type) && !memcmp(a.callsign, b.callsign, 9) && F(emergency_status) && F(uat_version) && F(sil) &&
           F(transmit_mso) && F(nac_p) && F(nac_v) && F(nic_baro) && F(has_cdti) && F(has_acas) && F(acas_ra_active) && F(ident_active) && F(atc_services) && F(heading_type) &&
           F(sec_altitude_type) && F(sec_altitude);
#undef F
}

static bool uplinkEq(const uat_uplink_mdb& a, const uat_uplink_mdb& b) {
#define F(x) (a.x == b.x)
    if (!(F(position_valid) && F(utc_coupled) && F(app_data_valid) && dEq(a.lat, b.lat) && dEq(a.lon, b.lon) && F(slot_id) && F(tisb_site_id))) return false;
    if (!a.app_data_valid) return true;
    if (memcmp(a.app_data, b.app_data, sizeof a.app_data) || a.num_info_frames != b.num_info_frames) return false;
    for (unsigned i = 0; i < UPLINK_MAX_INFO_FRAMES; i++) {
        const auto& x = a.info_frames[i];
        const auto& y = b.info_frames[i];
        if (x.length != y.length || x.type != y.type || x.is_fisb != y.is_fisb) return false;
        if (i >= a.num_info_frames) continue;   // slots past the count: only length / type are defined (compared above)
        if (x.data - a.app_data != y.data - b.app_data) return false;
        if (!x.is_fisb) continue;
        const auto& p = x.fisb;
        const auto& q = y.fisb;
        if (p.a_flag != q.a_flag || p.g_flag != q.g_flag || p.p_flag != q.p_flag || p.s_flag != q.s_flag || p.monthday_valid != q.monthday_valid ||
            p.seconds_valid != q.seconds_valid || p.product_id != q.product_id || p.month != q.month || p.day != q.day || p.hours != q.hours || p.minutes != q.minutes ||
            p.seconds != q.seconds || p.length != q.length || p.data - a.app_data != q.data - b.app_data)
            return false;
    }
    return true;
#undef F
}

static void hexTo(const char* hex, uint8_t* out, int n) {
    for (int i = 0; i < n; i++) {
        const int hi = hex[i * 2] >= 'A' ? hex[i * 2] - 'A' + 10 : hex[i * 2] - '0';
        const int lo = hex[i * 2 + 1] >= 'A' ? hex[i * 2 + 1] - 'A' + 10 : hex[i * 2 + 1] - '0';
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

static void checkAdsb(const uint8_t* fr, const char* what) {
    uint8_t a[UAT_UPLINK_FRAME_BYTES], b[UAT_UPLINK_FRAME_BYTES];
    memcpy(a, fr, 34); memcpy(b, fr, 34);
    uat_adsb_mdb x, y;
    memset(&x, 0x5A, sizeof x); memset(&y, 0xA5, sizeof y);   // both implementations start from a clean struct themselves
    uat_decode_adsb_mdb(a, &x);
    ref_uat_decode_adsb_mdb(b, &y);
    SAME(adsbEq(x, y), "adsb %s type %d", what, fr[0] >> 3);
}

static void checkUplink(const uint8_t* fr, bool dirty, const char* what) {
    uint8_t a[UAT_UPLINK_FRAME_BYTES], b[UAT_UPLINK_FRAME_BYTES];
    memcpy(a, fr, UAT_UPLINK_FRAME_DATA_BYTES); memcpy(b, fr, UAT_UPLINK_FRAME_DATA_BYTES);
    static uat_uplink_mdb x, y;
    memset(&x, dirty ? 0x6B : 0, sizeof x); memset(&y, dirty ? 0x6B : 0, sizeof y);
    uat_decode_uplink_mdb(a, &x);
    ref_uat_decode_uplink_mdb(b, &y);
    SAME(uplinkEq(x, y), "uplink %s dirty=%d", what, dirty);
}

static void mutate(uint8_t* d, int n) {
    for (int k = (int)(g() % 5); k > 0; k--) {
        switch (g() % 4) {
        case 0: d[g() % n] = (uint8_t)g(); break;
        case 1: d[g() % n] ^= (uint8_t)(1u << (g() % 8)); break;
        case 2: d[g() % n] = 0; break;
        default: d[g() % n] = 0xFF; break;
        }
    }
}

// Application data: a chain of information frames (9 bit length, type), FIS-B ones with a random APDU header, ended by an empty frame, overrun or the end.
static void buildApp(uint8_t* app) {
    memset(app, 0, 424);
    if (g() % 6 == 0) { for (int i = 0; i < 424; i++) app[i] = (uint8_t)g(); return; }
    int pos = 0;
    while (pos + 2 <= 424) {
        int len = (g() % 10 == 0) ? (int)(g() % 512) : (int)(g() % 40);
        if (g() % 12 == 0) len = 0;
        const int type = (g() % 4 == 0) ? (int)(g() % 16) : 0;
        app[pos] = (uint8_t)(len >> 1);
        app[pos + 1] = (uint8_t)(((len & 1) << 7) | (g() & 0x70) | type);
        for (int i = 0; i < len && pos + 2 + i < 424; i++) app[pos + 2 + i] = (uint8_t)g();
        if (g() % 20 == 0) break;
        pos += len + 2;
    }
}

int main() {
    const char* itc = getenv("ONAIR_DIFF_ITERS");
    const long iters = itc ? atol(itc) : 200000;
    uint8_t v[UAT_UPLINK_FRAME_BYTES];
    memset(v, 0, sizeof v);
    hexTo("007E6987D2D74238C1453855F89980C7524F", v, 18);
    checkAdsb(v, "DO-282 vector");
    // every message type with random contents, then mutated copies of the real vector, then truncated (zero tail) payloads
    for (long i = 0; i < iters; i++) {
        memset(v, 0, sizeof v);
        for (int k = 0; k < 34; k++) v[k] = (uint8_t)g();
        if (g() % 3 == 0) v[0] = (uint8_t)((g() % 32) << 3 | (g() % 8));
        if (g() % 4 == 0) { hexTo("007E6987D2D74238C1453855F89980C7524F", v, 18); mutate(v, 18); }
        if (g() % 5 == 0) { v[12] = (uint8_t)((v[12] & 0x3f) | ((g() % 4) << 6)); }   // air / ground state
        if (g() % 7 == 0) memset(v + 17, 0, 17);
        if (g() % 9 == 0) memset(v + 4, g() % 2 ? 0 : 0xFF, 8);
        checkAdsb(v, "random");
    }
    uint8_t up[UAT_UPLINK_FRAME_BYTES];
    for (long i = 0; i < iters; i++) {
        memset(up, 0, sizeof up);
        for (int k = 0; k < 8; k++) up[k] = (uint8_t)g();
        if (g() % 4) up[6] |= 0x20;
        buildApp(up + 8);
        if (g() % 8 == 0) mutate(up, UAT_UPLINK_FRAME_DATA_BYTES);
        // FIS-B APDU headers: force the time format field and flags in the first information frame
        if (g() % 2 && (up[8] | (up[9] & 0x80)) != 0) { up[10 + 1] = (uint8_t)((up[11] & 0xFC) | (g() & 3)); up[10 + 2] = (uint8_t)((up[12] & 0x7F) | ((g() & 1) << 7)); }
        checkUplink(up, i % 2 == 1, "random");
    }
    printf("test_uat_proto_rs: %ld checks, %ld mismatches\n", checks, fails);
    return fails ? 1 : 0;
}
