// Compares the ATSC transmit chain (randomiser, interleaver, trellis encoder, field sync) with the GNU Radio gr-dtv blocks.
// Not part of the build: compile it against gr-dtv sources with stub gnuradio headers (see docs). Result: bit-exact.
#define private public
#define protected public
#include "atsc_randomize.h"
#include "atsc_interleaver_impl.h"
#include "atsc_trellis_encoder_impl.h"
#include "atsc_field_sync_mux_impl.h"
#undef private
#undef protected
#include "dect2/atsc.h"
#include <cstdio>
#include <cstring>
#include <random>
using namespace gr::dtv;
namespace A = dect2::atsc;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
int main() {
    std::mt19937 rng(11);
    // randomiser
    {
        atsc_randomize r;
        A::Randomizer m;
        std::vector<uint8_t> a(187 * 40), b(187 * 40);
        for (auto& x : a) x = rng();
        b = a;
        atsc_mpeg_packet in; atsc_mpeg_packet_no_sync out;
        for (int p = 0; p < 40; p++) {
            in.data[0] = 0x47; memcpy(in.data + 1, &a[p * 187], 187);
            r.randomize(out, in);
            memcpy(&a[p * 187], out.data, 187);
        }
        m.apply(b.data(), b.size());
        CHECK(a == b, "randomiser differs");
        printf("randomiser %s\n", a == b ? "ok" : "MISMATCH");
    }
    // interleaver (encoder side, 3 fields of data, commutator reset per field)
    {
        atsc_interleaver_impl il;
        A::ByteInterleaver mi(false);
        const int nseg = 312 * 3;
        std::vector<atsc_mpeg_packet_rs_encoded> in(nseg), out(nseg);
        std::vector<uint8_t> mine(nseg * 207), flat(nseg * 207);
        for (int s = 0; s < nseg; s++) {
            in[s].pli.set_regular_seg(false, s % 312);
            for (int j = 0; j < 207; j++) { in[s].data[j] = rng(); flat[s * 207 + j] = in[s].data[j]; }
        }
        gr_vector_const_void_star ip = {in.data()}; gr_vector_void_star op = {out.data()};
        il.work(nseg, ip, op);
        for (int f = 0; f < 3; f++) { mi.syncCommutator(); mi.process(&flat[f * 312 * 207], &mine[f * 312 * 207], 312 * 207); }
        bool same = true;
        for (int s = 0; s < nseg; s++) if (memcmp(out[s].data, &mine[s * 207], 207) != 0) same = false;
        CHECK(same, "interleaver differs");
        printf("interleaver %s\n", same ? "ok" : "MISMATCH");
    }
    // trellis encoder + field sync mux vs the field encoder (3 fields; the reference never resets its encoders)
    {
        const int nf = 3;
        atsc_trellis_encoder_impl te;
        atsc_field_sync_mux_impl mux;
        A::FieldEncoder fe;
        // the field encoder does the whole chain; build the same chain with the reference blocks
        atsc_randomize rnd;
        atsc_interleaver_impl il;
        std::vector<uint8_t> ts((size_t)nf * 312 * 188);
        for (size_t p = 0; p < ts.size() / 188; p++) { ts[p * 188] = 0x47; for (int i = 1; i < 188; i++) ts[p * 188 + i] = rng(); }
        std::vector<atsc_mpeg_packet_rs_encoded> rs(nf * 312), ilv(nf * 312);
        // reed-solomon: use my encoder (checked separately)
        for (int f = 0; f < nf; f++) {
            rnd.reset();
            for (int p = 0; p < 312; p++) {
                atsc_mpeg_packet in; atsc_mpeg_packet_no_sync o;
                memcpy(in.data, &ts[((size_t)f * 312 + p) * 188], 188);
                rnd.randomize(o, in);
                A::rsEncode(o.data, rs[f * 312 + p].data);
                rs[f * 312 + p].pli.set_regular_seg(f & 1, p);
            }
        }
        gr_vector_const_void_star ip = {rs.data()}; gr_vector_void_star op = {ilv.data()};
        il.work(nf * 312, ip, op);
        std::vector<atsc_data_segment> seg(nf * 312), withSync;
        gr_vector_const_void_star ip2 = {ilv.data()}; gr_vector_void_star op2 = {seg.data()};
        te.work(nf * 312, ip2, op2);
        for (int i = 0; i < nf * 312; i++) seg[i].pli.set_regular_seg((i / 312) & 1, i % 312);
        std::vector<atsc_data_segment> outseg(nf * 313);
        gr_vector_int ni = {nf * 312};
        gr_vector_const_void_star ip3 = {seg.data()}; gr_vector_void_star op3 = {outseg.data()};
        const int made = mux.general_work(nf * 313, ni, ip3, op3);
        CHECK(made == nf * 313, "mux produced %d", made);
        bool all = true;
        std::vector<uint8_t> sym(A::kFieldSyms);
        for (int f = 0; f < nf; f++) {
            fe.encode(&ts[(size_t)f * 312 * 188], f & 1, sym.data());
            for (int s = 0; s < 313; s++) {
                if (memcmp(&sym[s * 832], outseg[f * 313 + s].data, 832) != 0) { if (all) printf("field %d segment %d differs\n", f, s); all = false; }
            }
        }
        CHECK(all, "field encoder differs from the reference chain");
        printf("trellis + field sync %s\n", all ? "ok" : "MISMATCH");
    }
    printf(fails ? "FAILED\n" : "all ok\n");
    return fails;
}
