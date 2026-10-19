// KAAL streaming receiver, loopback: the transmit signal built from the PHY functions is fed in 1000-sample chunks.
// Clean: the audio equals the codec-only decode bit for bit. Noise (Eb/N0 8 and 10 dB), a frequency offset of +-200 Hz and a start in
// the middle of the stream: the frame error rate stays low.
#include "dect2/kaal/kaal_rx.h"
#include "kaal_rx_gen.h"
#include <algorithm>
#include <cstdio>
using namespace dect2;
#include "../common/test_util.h"

struct Run {
    std::vector<int16_t> audio;
    kaal::RxStats st;
};

static Run run(const std::vector<std::complex<float>>& sig, size_t from) {
    kaal::VoiceRx rx;
    rx.configure(48000.0);
    Run r;
    std::vector<int16_t> buf(4096);
    for (size_t i = from; i < sig.size(); i += 1000) {
        rx.feed(&sig[i], std::min<size_t>(1000, sig.size() - i));
        for (size_t n; (n = rx.popAudio(buf.data(), buf.size())) > 0;) r.audio.insert(r.audio.end(), buf.begin(), buf.begin() + (std::ptrdiff_t)n);
    }
    r.st = rx.stats();
    return r;
}

static double fer(const kaal::RxStats& s) {
    const double n = (double)(s.framesGood + s.framesBad);
    return n > 0 ? (double)s.framesBad / n : 1.0;
}

int main() {
    const std::vector<int16_t> pcm = kaalrx::testPcm(16000 * 6);
    const std::vector<uint8_t> bits = kaalrx::frameBits(pcm);
    double pw = 0;
    const std::vector<kaalrx::cd> tx = kaalrx::transmit(bits, pw);
    printf("signal: %zu samples (%.1f s), %zu frames\n", tx.size(), tx.size() / 48000.0, bits.size() / 320);

    KaalCodec codec;
    const std::vector<int16_t> all = codec.decode(bits.data(), bits.size() / 320);
    const std::vector<int16_t> expected(all.begin() + KaalCodec::kDelay, all.end());

    {   // clean: bit exact against the codec alone
        const Run r = run(kaalrx::channel(tx, pw, 60.0, 0.0, 1), 0);
        printf("clean: %zu of %zu samples, frames good %llu bad %llu, snr %.1f dB, sweep %.1f dB\n", r.audio.size(), expected.size(),
               (unsigned long long)r.st.framesGood, (unsigned long long)r.st.framesBad, r.st.snrDb, r.st.sweepDb);
        CHECK(r.st.framesBad == 0, "bad frames %llu", (unsigned long long)r.st.framesBad);
        CHECK(r.audio.size() >= expected.size() * 3 / 4, "audio %zu of %zu", r.audio.size(), expected.size());
        size_t diff = 0;
        for (size_t i = 0; i < r.audio.size() && i < expected.size(); i++) diff += r.audio[i] != expected[i];
        CHECK(diff == 0, "%zu samples differ from the codec-only output", diff);
    }
    {   // Eb/N0 8 dB
        const Run r = run(kaalrx::channel(tx, pw, 8.0, 0.0, 2), 0);
        printf("8 dB: frames good %llu bad %llu (FER %.3f), snr est %.1f dB\n", (unsigned long long)r.st.framesGood, (unsigned long long)r.st.framesBad, fer(r.st), r.st.snrDb);
        CHECK(r.st.framesGood + r.st.framesBad >= 150, "frames %llu", (unsigned long long)(r.st.framesGood + r.st.framesBad));
        CHECK(fer(r.st) < 0.05, "FER %.3f", fer(r.st));
    }
    for (double cfo : {-200.0, 200.0}) {   // frequency offset
        const Run r = run(kaalrx::channel(tx, pw, 10.0, cfo, 3), 0);
        printf("cfo %+.0f Hz: frames good %llu bad %llu (FER %.3f), cfo est %.1f Hz\n", cfo, (unsigned long long)r.st.framesGood,
               (unsigned long long)r.st.framesBad, fer(r.st), r.st.cfoHz);
        CHECK(r.st.framesGood + r.st.framesBad >= 150, "frames %llu", (unsigned long long)(r.st.framesGood + r.st.framesBad));
        CHECK(fer(r.st) < 0.05, "FER %.3f at %+.0f Hz", fer(r.st), cfo);
        CHECK(std::fabs(r.st.cfoHz - cfo) < 30.0, "cfo estimate %.1f", r.st.cfoHz);
    }
    {   // start in the middle of the stream (not on a frame or sweep boundary)
        const Run r = run(kaalrx::channel(tx, pw, 10.0, 0.0, 4), 100003);
        printf("mid start: frames good %llu bad %llu (FER %.3f), audio %zu samples\n", (unsigned long long)r.st.framesGood, (unsigned long long)r.st.framesBad,
               fer(r.st), r.audio.size());
        CHECK(r.st.framesGood >= 60, "good frames %llu", (unsigned long long)r.st.framesGood);
        CHECK(fer(r.st) < 0.05, "FER %.3f", fer(r.st));
        CHECK(r.audio.size() >= 16000, "audio %zu", r.audio.size());
    }
    return testSummary("kaal_rx_loopback");
}
