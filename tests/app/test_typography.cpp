// The typography table is monotonic and all five embedded fonts load (the Clean look) with the Latin, Cyrillic and Greek ranges the UI needs.
#include "typography.h"
#include "imgui.h"
#include <cstdio>
#include <initializer_list>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main() {
    for (int i = 1; i < typo::kSizeCount; i++) CHECK(typo::kSizes[i] > typo::kSizes[i - 1]);
    CHECK(typo::kSizes[typo::Body] == 13 && typo::kSizes[typo::Hero] == 32);
    typo::setVariant(typo::Clean);
    CHECK(typo::variant() == typo::Clean && typo::baseSize() == 14.f);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    CHECK(typo::loadFonts(io, nullptr));
    const typo::Fonts& f = typo::fonts();
    ImFont* all[] = {f.regular, f.medium, f.semibold, f.mono, f.monoBold};
    for (ImFont* x : all) {
        CHECK(x != nullptr);
        if (!x) continue;
        for (ImWchar c : {(ImWchar)'A', (ImWchar)0x0105 /* Lithuanian/Polish */, (ImWchar)0x0151 /* Hungarian */, (ImWchar)0x0416 /* Cyrillic */, (ImWchar)0x03A9 /* Greek */, (ImWchar)0x00B0})
            CHECK(x->IsGlyphInFont(c));
    }
    ImGui::DestroyContext();
    printf(fails ? "typography: %d failures\n" : "typography: ok\n", fails);
    return fails != 0;
}
