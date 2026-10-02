// Tiny software renderer for Dear ImGui draw data (headless screenshots / video capture of the real UI).
#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

struct SwTex { int w = 0, h = 0; std::vector<uint32_t> px; SwTex() {} SwTex(int W, int H) : w(W), h(H), px((size_t)W * H, 0xFF000000u) {} };
struct SwFb { int w = 0, h = 0; std::vector<uint32_t> px; void init(int W, int H) { w = W; h = H; px.assign((size_t)W * H, 0xFF000000u); } };

namespace swr {
static inline void sample(const SwTex* t, float u, float v, float& r, float& g, float& b, float& a) {
    float x = u * t->w - 0.5f, y = v * t->h - 0.5f;
    int x0 = (int)std::floor(x), y0 = (int)std::floor(y); float fx = x - x0, fy = y - y0;
    auto px = [&](int xx, int yy) { xx = std::min(t->w - 1, std::max(0, xx)); yy = std::min(t->h - 1, std::max(0, yy)); return t->px[(size_t)yy * t->w + xx]; };
    uint32_t c00 = px(x0, y0), c10 = px(x0 + 1, y0), c01 = px(x0, y0 + 1), c11 = px(x0 + 1, y0 + 1);
    float w00 = (1 - fx) * (1 - fy), w10 = fx * (1 - fy), w01 = (1 - fx) * fy, w11 = fx * fy;
    auto ch = [&](int s) { return (float)((c00 >> s) & 255) * w00 + (float)((c10 >> s) & 255) * w10 + (float)((c01 >> s) & 255) * w01 + (float)((c11 >> s) & 255) * w11; };
    r = ch(0); g = ch(8); b = ch(16); a = ch(24);
}
static inline void blend(uint32_t& d, float r, float g, float b, float a) { // straight alpha over opaque dst
    if (a <= 0) return;
    float k = a * (1.f / 255.f);
    float dr = d & 255, dg = (d >> 8) & 255, db = (d >> 16) & 255;
    int R = (int)(r * k + dr * (1 - k) + .5f), G = (int)(g * k + dg * (1 - k) + .5f), B = (int)(b * k + db * (1 - k) + .5f);
    d = 0xFF000000u | (uint32_t)(B << 16) | (uint32_t)(G << 8) | (uint32_t)R;
}
}

static void swUploadTextures(ImDrawData* dd) {
    if (!dd->Textures) return;
    for (ImTextureData* t : *dd->Textures) {
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
            SwTex* s = (SwTex*)(uintptr_t)t->BackendUserData;
            if (!s) { s = new SwTex(t->Width, t->Height); t->BackendUserData = s; t->SetTexID((ImTextureID)(uintptr_t)s); }
            const unsigned char* p = (const unsigned char*)t->GetPixels();
            for (int i = 0; i < t->Width * t->Height; i++) {
                if (t->Format == ImTextureFormat_Alpha8) s->px[i] = 0x00FFFFFFu | ((uint32_t)p[i] << 24);
                else s->px[i] = ((const uint32_t*)p)[i];
            }
            t->SetStatus(ImTextureStatus_OK);
        } else if (t->Status == ImTextureStatus_WantDestroy && t->UnusedFrames > 0) {
            delete (SwTex*)t->BackendUserData; t->BackendUserData = nullptr; t->SetTexID(ImTextureID_Invalid); t->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

static void swRender(ImDrawData* dd, SwFb& fb, float scale) {
    swUploadTextures(dd);
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList* dl = dd->CmdLists[n];
        const ImDrawVert* vb0 = dl->VtxBuffer.Data; const ImDrawIdx* ib = dl->IdxBuffer.Data;
        for (const ImDrawCmd& cmd : dl->CmdBuffer) {
            if (cmd.UserCallback) continue;
            int cx0 = std::max(0, (int)std::floor(cmd.ClipRect.x * scale)), cy0 = std::max(0, (int)std::floor(cmd.ClipRect.y * scale));
            int cx1 = std::min(fb.w, (int)std::ceil(cmd.ClipRect.z * scale)), cy1 = std::min(fb.h, (int)std::ceil(cmd.ClipRect.w * scale));
            if (cx1 <= cx0 || cy1 <= cy0) continue;
            const ImDrawVert* vb = vb0 + cmd.VtxOffset;
            const SwTex* tex = (const SwTex*)(uintptr_t)cmd.GetTexID();
            if (!tex) continue;
            for (unsigned i = 0; i + 2 < cmd.ElemCount; ) {
                const ImDrawVert* a = vb + ib[cmd.IdxOffset + i];
                const ImDrawVert* b = vb + ib[cmd.IdxOffset + i + 1];
                const ImDrawVert* c = vb + ib[cmd.IdxOffset + i + 2];
                // fast path: two triangles forming an axis-aligned rectangle with one colour
                if (i + 5 < cmd.ElemCount) {
                    const ImDrawVert* d = vb + ib[cmd.IdxOffset + i + 3]; const ImDrawVert* e = vb + ib[cmd.IdxOffset + i + 4]; const ImDrawVert* f = vb + ib[cmd.IdxOffset + i + 5];
                    const ImDrawVert* vs[6] = {a, b, c, d, e, f};
                    float mnx = 1e30f, mxx = -1e30f, mny = 1e30f, mxy = -1e30f; bool same = true, aligned = true;
                    for (auto v : vs) { mnx = std::min(mnx, v->pos.x); mxx = std::max(mxx, v->pos.x); mny = std::min(mny, v->pos.y); mxy = std::max(mxy, v->pos.y); same &= v->col == a->col;
                        aligned &= (std::fabs(v->pos.x - mnx) < 1e-3f || true); }
                    for (auto v : vs) aligned &= (std::fabs(v->pos.x - mnx) < 1e-3f || std::fabs(v->pos.x - mxx) < 1e-3f) && (std::fabs(v->pos.y - mny) < 1e-3f || std::fabs(v->pos.y - mxy) < 1e-3f);
                    if (same && aligned && mxx > mnx && mxy > mny) {
                        // uv at the min / max corners
                        float u0 = 0, u1 = 0, v0 = 0, v1 = 0;
                        for (auto v : vs) { if (std::fabs(v->pos.x - mnx) < 1e-3f) u0 = v->uv.x; else u1 = v->uv.x; if (std::fabs(v->pos.y - mny) < 1e-3f) v0 = v->uv.y; else v1 = v->uv.y; }
                        const bool constUv = std::fabs(u1 - u0) < 1e-7f && std::fabs(v1 - v0) < 1e-7f;
                        float cr = a->col & 255, cg = (a->col >> 8) & 255, cb = (a->col >> 16) & 255, ca = (a->col >> 24) & 255;
                        int x0 = std::max(cx0, (int)std::ceil(mnx * scale - 0.5f)), x1 = std::min(cx1, (int)std::ceil(mxx * scale - 0.5f));
                        int y0 = std::max(cy0, (int)std::ceil(mny * scale - 0.5f)), y1 = std::min(cy1, (int)std::ceil(mxy * scale - 0.5f));
                        float sr = 0, sg = 0, sb = 0, sa = 0;
                        if (constUv) { swr::sample(tex, u0, v0, sr, sg, sb, sa); sr = sr * cr / 255; sg = sg * cg / 255; sb = sb * cb / 255; sa = sa * ca / 255; }
                        const bool nearest = !constUv && std::fabs((u1 - u0) * tex->w / ((mxx - mnx) * scale) - 1.f) < 0.03f && std::fabs((v1 - v0) * tex->h / ((mxy - mny) * scale) - 1.f) < 0.03f;
                        if (nearest) {
                            for (int y = y0; y < y1; y++) {
                                int ty = (int)std::floor((v0 + ((y + .5f) / scale - mny) / (mxy - mny) * (v1 - v0)) * tex->h); ty = std::min(tex->h - 1, std::max(0, ty));
                                const uint32_t* trow = &tex->px[(size_t)ty * tex->w]; uint32_t* row = &fb.px[(size_t)y * fb.w];
                                for (int x = x0; x < x1; x++) {
                                    int tx = (int)std::floor((u0 + ((x + .5f) / scale - mnx) / (mxx - mnx) * (u1 - u0)) * tex->w); tx = std::min(tex->w - 1, std::max(0, tx));
                                    uint32_t tc = trow[tx]; float ta = (float)(tc >> 24) * ca / 255;
                                    if (ta <= 0) continue;
                                    swr::blend(row[x], (float)(tc & 255) * cr / 255, (float)((tc >> 8) & 255) * cg / 255, (float)((tc >> 16) & 255) * cb / 255, ta);
                                }
                            }
                            i += 6; continue;
                        }
                        for (int y = y0; y < y1; y++) {
                            float fv = v0 + ((y + .5f) / scale - mny) / (mxy - mny) * (v1 - v0);
                            uint32_t* row = &fb.px[(size_t)y * fb.w];
                            for (int x = x0; x < x1; x++) {
                                if (constUv) { if (sa >= 254.5f) row[x] = 0xFF000000u | ((uint32_t)sb << 16) | ((uint32_t)sg << 8) | (uint32_t)sr; else swr::blend(row[x], sr, sg, sb, sa); }
                                else { float fu = u0 + ((x + .5f) / scale - mnx) / (mxx - mnx) * (u1 - u0); float r, g, bb, aa; swr::sample(tex, fu, fv, r, g, bb, aa); swr::blend(row[x], r * cr / 255, g * cg / 255, bb * cb / 255, aa * ca / 255); }
                            }
                        }
                        i += 6; continue;
                    }
                }
                // generic triangle
                float ax = a->pos.x * scale, ay = a->pos.y * scale, bx = b->pos.x * scale, by = b->pos.y * scale, qx = c->pos.x * scale, qy = c->pos.y * scale;
                float area = (bx - ax) * (qy - ay) - (by - ay) * (qx - ax);
                i += 3;
                if (std::fabs(area) < 1e-6f) continue;
                int x0 = std::max(cx0, (int)std::floor(std::min({ax, bx, qx}))), x1 = std::min(cx1, (int)std::ceil(std::max({ax, bx, qx})));
                int y0 = std::max(cy0, (int)std::floor(std::min({ay, by, qy}))), y1 = std::min(cy1, (int)std::ceil(std::max({ay, by, qy})));
                float inv = 1.f / area;
                auto col = [](uint32_t cc, int s) { return (float)((cc >> s) & 255); };
                for (int y = y0; y < y1; y++) {
                    float py = y + .5f; uint32_t* row = &fb.px[(size_t)y * fb.w];
                    // x-span of the triangle on this scanline
                    float xl = 1e30f, xr = -1e30f;
                    const float ex[3][4] = {{ax, ay, bx, by}, {bx, by, qx, qy}, {qx, qy, ax, ay}};
                    for (auto& e : ex) {
                        float ya = e[1], yb = e[3];
                        if ((py < std::min(ya, yb)) || (py > std::max(ya, yb)) || ya == yb) continue;
                        float xx = e[0] + (py - ya) / (yb - ya) * (e[2] - e[0]);
                        xl = std::min(xl, xx); xr = std::max(xr, xx);
                    }
                    if (xl > xr) continue;
                    int xs = std::max(x0, (int)std::floor(xl - 0.5f)), xe = std::min(x1, (int)std::ceil(xr + 0.5f));
                    for (int x = xs; x < xe; x++) {
                        float px = x + .5f;
                        float w1 = ((px - ax) * (qy - ay) - (py - ay) * (qx - ax)) * inv;      // weight of b
                        float w2 = ((bx - ax) * (py - ay) - (by - ay) * (px - ax)) * inv;      // weight of c
                        float w0 = 1.f - w1 - w2;
                        if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                        float u = a->uv.x * w0 + b->uv.x * w1 + c->uv.x * w2, v = a->uv.y * w0 + b->uv.y * w1 + c->uv.y * w2;
                        float r, g, bb, aa; swr::sample(tex, u, v, r, g, bb, aa);
                        float cr = col(a->col, 0) * w0 + col(b->col, 0) * w1 + col(c->col, 0) * w2, cg = col(a->col, 8) * w0 + col(b->col, 8) * w1 + col(c->col, 8) * w2;
                        float cb = col(a->col, 16) * w0 + col(b->col, 16) * w1 + col(c->col, 16) * w2, ca = col(a->col, 24) * w0 + col(b->col, 24) * w1 + col(c->col, 24) * w2;
                        swr::blend(row[x], r * cr / 255, g * cg / 255, bb * cb / 255, aa * ca / 255);
                    }
                }
            }
        }
    }
}
