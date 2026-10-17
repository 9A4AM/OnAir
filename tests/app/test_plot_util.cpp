// Pure plot helpers: trace decimation keeps narrow peaks, frequency ticks get the decimals their step needs, units adapt.
#include "../../app/gfx/plot_util.h"
#include <cstdio>
static int fails = 0;
static void check(bool ok, const char* w) { if (!ok) { printf("FAIL: %s\n", w); fails++; } }
int main() {
    std::vector<float> y(100000, -90.f);
    y[54321] = -20.f; y[7000] = -140.f;   // a one-bin spike and a one-bin notch
    std::vector<double> ox, oy;
    plt::envelope(y, 100.0, 1e-4, 100.0, 110.0, 800, ox, oy);
    check(ox.size() == oy.size() && ox.size() <= 1600, "envelope size bounded by 2 per column");
    float mx = -1e9f, mn = 1e9f; for (double v : oy) { mx = std::max(mx, (float)v); mn = std::min(mn, (float)v); }
    check(mx == -20.f && mn == -140.f, "envelope keeps the narrow peak and notch");
    for (size_t i = 1; i < ox.size(); i++) check(ox[i] >= ox[i - 1], "envelope x ascending");
    plt::envelope(y, 100.0, 1e-4, 100.0, 100.01, 800, ox, oy);   // zoomed in: every bin
    check(ox.size() >= 100 && ox.size() < 110, "envelope zoomed in keeps bins");
    plt::envelope(std::vector<float>{1.f}, 0, 1, 0, 1, 10, ox, oy);
    check(ox.empty(), "envelope of one bin is empty");
    check(plt::formatFreqTick(100.5, 0.5) == "100.5", "tick 0.5 MHz step");
    check(plt::formatFreqTick(100.005, 0.005) == "100.005", "tick 5 kHz step");
    check(plt::formatFreqTick(102, 2) == "102", "tick 2 MHz step");
    check(plt::formatFreqTick(0, 0.1) == "0.0", "tick zero");
    check(plt::formatFreqUnit(1200) == "1.2000 GHz", "unit GHz");
    check(plt::formatFreqUnit(100.5) == "100.5000 MHz", "unit MHz");
    check(plt::formatFreqUnit(0.0125) == "12.50 kHz", "unit kHz");
    check(plt::formatFreqUnit(0.0008) == "800 Hz", "unit Hz");
    check(plt::fillBottomAlpha(0.85f) > 0.149f && plt::fillBottomAlpha(0.85f) < 0.151f && plt::fillBottomAlpha(2) == 0 && plt::fillBottomAlpha(-1) == 1, "fill alpha clamp");
    if (!fails) printf("plot_util ok\n");
    return fails ? 1 : 0;
}
