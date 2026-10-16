// Audio capture: a ring for the microphone, a looped file (WAV / MP3 through FFmpeg), a tone, or silence.
#include "audioin_impl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include "dect2/common/mathconst.h"
}

namespace dect2 {

namespace {
using dect2::math::kPi;

bool loadPcmWav(const std::string& path, std::vector<float>& stereo, int wantRate) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char riff[12];
    if (fread(riff, 1, 12, f) != 12 || memcmp(riff, "RIFF", 4) || memcmp(riff + 8, "WAVE", 4)) { fclose(f); return false; }
    int16_t ch = 1, bits = 16;
    int32_t sr = 8000;
    std::vector<float> mono;
    for (;;) {
        char id[4];
        uint32_t n = 0;
        if (fread(id, 1, 4, f) != 4 || fread(&n, 4, 1, f) != 1) { fclose(f); return false; }
        if (!memcmp(id, "fmt ", 4)) {
            uint8_t fmt[16] = {};
            if (n < 16 || fread(fmt, 1, 16, f) != 16) { fclose(f); return false; }
            if (n > 16) fseek(f, (long)n - 16, SEEK_CUR);
            memcpy(&ch, fmt + 2, 2);
            memcpy(&sr, fmt + 4, 4);
            memcpy(&bits, fmt + 14, 2);
        } else if (!memcmp(id, "data", 4)) {
            if (bits != 16) { fclose(f); return false; }
            std::vector<int16_t> raw(n / 2);
            if (fread(raw.data(), 2, raw.size(), f) != raw.size()) { fclose(f); return false; }
            fclose(f);
            const int c = std::max(1, (int)ch);
            const size_t frames = raw.size() / (size_t)c;
            mono.resize(frames);
            for (size_t i = 0; i < frames; i++) {
                float s = 0;
                for (int k = 0; k < c; k++) s += raw[i * (size_t)c + (size_t)k] / 32768.f;
                mono[i] = s / (float)c;
            }
            break;
        } else fseek(f, (long)n, SEEK_CUR);
    }
    if (mono.empty()) return false;
    const double adv = (double)sr / (double)wantRate;
    stereo.resize((size_t)wantRate * 2 * mono.size() / (size_t)std::max(1, sr) + 4);
    size_t o = 0;
    double pos = 0;
    while (pos + 1 < (double)mono.size() && o + 1 < stereo.size()) {
        const size_t i = (size_t)pos;
        const float a = mono[i], b = mono[std::min(i + 1, mono.size() - 1)];
        const float s = a + (b - a) * (float)(pos - (double)i);
        stereo[o++] = s;
        stereo[o++] = s;
        pos += adv;
    }
    stereo.resize(o);
    return o >= 2;
}

bool loadFfmpeg(const std::string& path, std::vector<float>& stereo, int wantRate) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return false; }
    int si = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (si < 0) { avformat_close_input(&fmt); return false; }
    AVStream* st = fmt->streams[si];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, codec, nullptr) < 0) {
        if (ctx) avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return false;
    }
    AVChannelLayout outL;
    av_channel_layout_default(&outL, 2);
    SwrContext* swr = nullptr;
    swr_alloc_set_opts2(&swr, &outL, AV_SAMPLE_FMT_FLT, wantRate, &ctx->ch_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    if (!swr || swr_init(swr) < 0) {
        if (swr) swr_free(&swr);
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        av_channel_layout_uninit(&outL);
        return false;
    }
    AVPacket* pk = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    stereo.clear();
    auto append = [&](AVFrame* f) {
        uint8_t* out[1] = {nullptr};
        const int maxOut = (int)((int64_t)f->nb_samples * wantRate / std::max(1, ctx->sample_rate) + 256);
        std::vector<float> buf((size_t)maxOut * 2);
        out[0] = (uint8_t*)buf.data();
        const int got = swr_convert(swr, out, maxOut, (const uint8_t**)f->extended_data, f->nb_samples);
        if (got > 0) stereo.insert(stereo.end(), buf.begin(), buf.begin() + got * 2);
    };
    while (av_read_frame(fmt, pk) >= 0) {
        if (pk->stream_index != si) { av_packet_unref(pk); continue; }
        if (avcodec_send_packet(ctx, pk) == 0) {
            while (avcodec_receive_frame(ctx, fr) == 0) append(fr);
        }
        av_packet_unref(pk);
        if (stereo.size() > (size_t)wantRate * 2 * 600) break;   // cap 10 minutes
    }
    avcodec_send_packet(ctx, nullptr);
    while (avcodec_receive_frame(ctx, fr) == 0) append(fr);
    av_frame_free(&fr);
    av_packet_free(&pk);
    swr_free(&swr);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    av_channel_layout_uninit(&outL);
    return stereo.size() >= 2;
}

void measure(std::atomic<float>& peak, const float* s, int n, float gain) {
    float p = peak.load(std::memory_order_relaxed) * 0.92f;
    for (int i = 0; i < n * 2; i++) p = std::max(p, std::fabs(s[i] * gain));
    peak.store(std::min(p, 1.f), std::memory_order_relaxed);
}
}

void AudioIn::Impl::push(const float* in, uint32_t nFrames) {
    const uint64_t wr = w.load(std::memory_order_relaxed), rd = r.load(std::memory_order_acquire);
    const int space = (int)(frames - (wr - rd));
    const int n = std::min((int)nFrames, std::max(0, space));
    const float g = gain.load(std::memory_order_relaxed);
    const size_t mask = frames - 1;
    for (int i = 0; i < n; i++) {
        const size_t k = (size_t)((wr + i) & mask) * 2;
        ring[k] = in[2 * i] * g;
        ring[k + 1] = in[2 * i + 1] * g;
    }
    w.store(wr + (uint64_t)n, std::memory_order_release);
    measure(peak, in, n, g);
}

AudioIn::AudioIn() : p_(new Impl) {
    p_->frames = 1 << 16;
    p_->ring.assign(p_->frames * 2, 0.f);
}
AudioIn::~AudioIn() { stop(); }

bool AudioIn::start(Kind k, int sampleRate, const std::string& pathOrDevice, double toneHz, std::string& err) {
    stop();
    rate_ = sampleRate > 0 ? sampleRate : 48000;
    kind_ = k;
    p_->toneHz = toneHz > 0 ? toneHz : 1000;
    p_->tph = 0;
    p_->file.clear();
    p_->filePos = 0;
    p_->w = 0;
    p_->r = 0;
    p_->peak = 0;
    if (k == Mic) {
        if (!audioInMicStart(p_.get(), rate_, pathOrDevice, err)) { kind_ = None; return false; }
        running_ = true;
        return true;
    }
    if (k == File) {
        if (!loadFfmpeg(pathOrDevice, p_->file, rate_) && !loadPcmWav(pathOrDevice, p_->file, rate_)) {
            err = "could not open the audio file (WAV or MP3)";
            kind_ = None;
            return false;
        }
        running_ = true;
        return true;
    }
    if (k == Tone || k == Silence) {
        running_ = true;
        return true;
    }
    err = "no audio source";
    kind_ = None;
    return false;
}

void AudioIn::stop() {
    if (kind_ == Mic) audioInMicStop(p_.get());
    running_ = false;
    kind_ = None;
    p_->file.clear();
}

void AudioIn::setMuted(bool m) { p_->muted.store(m); }
void AudioIn::setGain(float g) { p_->gain.store(g); }
float AudioIn::peak() const { return p_->peak.load(); }

int AudioIn::read(float* stereo, int n) {
    if (n <= 0) return 0;
    const float g = p_->gain.load();
    const bool mute = p_->muted.load();
    if (kind_ == File && !p_->file.empty()) {
        for (int i = 0; i < n; i++) {
            const size_t k = p_->filePos % (p_->file.size() / 2);
            const float l = p_->file[k * 2] * g, r = p_->file[k * 2 + 1] * g;
            stereo[2 * i] = mute ? 0 : l;
            stereo[2 * i + 1] = mute ? 0 : r;
            p_->filePos++;
        }
        measure(p_->peak, stereo, n, 1.f);
        return n;
    }
    if (kind_ == Tone) {
        const double w = 2 * kPi * p_->toneHz / rate_;
        for (int i = 0; i < n; i++) {
            const float s = mute ? 0.f : 0.5f * (float)std::sin(p_->tph) * g;
            stereo[2 * i] = stereo[2 * i + 1] = s;
            p_->tph += w;
            if (p_->tph > 2 * kPi) p_->tph -= 2 * kPi;
        }
        measure(p_->peak, stereo, n, 1.f);
        return n;
    }
    if (kind_ == Silence) {
        memset(stereo, 0, (size_t)n * 2 * sizeof(float));
        p_->peak.store(p_->peak.load() * 0.9f);
        return n;
    }
    if (kind_ == Mic) {
        const uint64_t wr = p_->w.load(std::memory_order_acquire), rd = p_->r.load(std::memory_order_relaxed);
        const int have = (int)std::min<uint64_t>(wr - rd, (uint64_t)n);
        const size_t mask = p_->frames - 1;
        for (int i = 0; i < have; i++) {
            const size_t k = (size_t)((rd + i) & mask) * 2;
            stereo[2 * i] = mute ? 0 : p_->ring[k];
            stereo[2 * i + 1] = mute ? 0 : p_->ring[k + 1];
        }
        for (int i = have; i < n; i++) stereo[2 * i] = stereo[2 * i + 1] = 0;
        p_->r.store(rd + (uint64_t)have, std::memory_order_release);
        return n;
    }
    memset(stereo, 0, (size_t)n * 2 * sizeof(float));
    return n;
}

std::vector<AudioInputDevice> listAudioInputs() {
    std::vector<AudioInputDevice> v;
    v.push_back({"", "Default input"});
    auto rest = audioInListMic();
    v.insert(v.end(), rest.begin(), rest.end());
    return v;
}

} // namespace dect2
