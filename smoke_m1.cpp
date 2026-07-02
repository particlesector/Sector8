// tests/smoke_m1.cpp
//
// Sector8 M1 smoke test — no SDL, no Lua. Drives SoftFpga directly, dumps a
// 320x180 PPM, and asserts known pixels (BG tiling, sub-palette, sprite
// color-key, backdrop) plus the 16-sprites-per-line cap.
//
//   g++ -std=c++20 soft_fpga.cpp smoke_m1.cpp -o smoke && ./smoke out.ppm

#include "soft_fpga.h"
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace s8;

// Expected-color helper mirroring SoftFpga's 12->24 bit expansion (0xRRGGBBAA).
static uint32_t rgb(int r, int g, int b) {
    return uint32_t(((r << 4 | r) << 24) | ((g << 4 | g) << 16) |
                    ((b << 4 | b) << 8) | 0xFF);
}

// Pack an 8x8 index grid into 32 bytes, 4bpp, left pixel in the high nibble.
static void packTile(const int (&px)[8][8], uint8_t out[32]) {
    int o = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; x += 2)
            out[o++] = uint8_t((px[y][x] << 4) | px[y][x + 1]);
}

static void writePPM(const char* path, VideoFrame f) {
    FILE* fp = std::fopen(path, "wb");
    std::fprintf(fp, "P6\n%d %d\n255\n", f.width, f.height);
    for (int i = 0; i < f.width * f.height; ++i) {
        uint32_t p = f.pixels[i];
        uint8_t rgb3[3] = { uint8_t(p >> 24), uint8_t(p >> 16), uint8_t(p >> 8) };
        std::fwrite(rgb3, 1, 3, fp);
    }
    std::fclose(fp);
}

// --- tiles -----------------------------------------------------------------
static const int kBox[8][8] = {     // tile 1: border idx1, interior idx2
    {1,1,1,1,1,1,1,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},
    {1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,1,1,1,1,1,1,1},
};
static const int kDiamond[8][8] = { // tile 2: idx1 shape, idx0 transparent
    {0,0,0,1,1,0,0,0},{0,0,1,1,1,1,0,0},{0,1,1,1,1,1,1,0},{1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},{0,1,1,1,1,1,1,0},{0,0,1,1,1,1,0,0},{0,0,0,1,1,0,0,0},
};

static void loadTiles(SoftFpga& d) {
    uint8_t blob[96] = {0};                 // tile0 = blank (all transparent)
    packTile(kBox,     blob + 32);          // tile1
    packTile(kDiamond, blob + 64);          // tile2
    d.loadBegin(0, sizeof blob);
    d.loadData(std::span<const uint8_t>(blob, sizeof blob));
    d.loadEnd(0);
    d.bankActivate(0);
}

static void loadPalette(SoftFpga& d) {
    Color12 base[3] = { {1,1,4}, {15,15,15}, {3,12,4} };   // navy / white / green
    d.setPalette(0, base);
    Color12 sub1[2] = { {0,0,0}, {15,2,2} };               // entry17 = red
    d.setPalette(16, sub1);
}

static int failures = 0;
static void check(const char* what, uint32_t got, uint32_t want) {
    bool ok = (got == want);
    std::printf("  [%s] %-22s got=%08X want=%08X\n", ok ? "PASS" : "FAIL",
                what, got, want);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "smoke_m1.ppm";

    SoftFpga d;
    loadTiles(d);
    loadPalette(d);

    // Fill BG0 with the box tile, then punch a transparent hole to expose backdrop.
    std::vector<TileCell> map(64 * 64);
    for (auto& c : map) { c.tile = 1; c.palette = 0; }
    map[10 * 64 + 20].tile = 0;                 // screen cell (20,10) -> backdrop
    d.writeTilemap(0, 0, map);

    // Sprites: a red diamond, a flipped one, and one hanging off the left edge.
    std::vector<OamEntry> oam;
    auto sprite = [](int x, int y, bool fh) {
        OamEntry s; s.x = int16_t(x); s.y = int16_t(y); s.tile = 2;
        s.palette = 1; s.flip_h = fh; s.enabled = true; return s;
    };
    oam.push_back(sprite(100, 80, false));
    oam.push_back(sprite(150, 50, true));
    oam.push_back(sprite(-4, 100, false));      // partial off-screen left
    d.writeOam(oam);

    d.swap();
    VideoFrame f = d.frame();
    writePPM(out, f);
    std::printf("wrote %s (%dx%d)\n\n", out, f.width, f.height);

    auto at = [&](int x, int y) { return f.pixels[y * f.width + x]; };

    std::printf("scene assertions:\n");
    check("bg border",     at(0, 0),     rgb(15,15,15));   // box top edge -> white
    check("bg interior",   at(4, 4),     rgb(3,12,4));     // box middle  -> green
    check("backdrop hole", at(162, 82),  rgb(1,1,4));      // transparent -> navy
    check("sprite center", at(104, 84),  rgb(15,2,2));     // diamond     -> red
    check("sprite keyhole",at(100, 80),  rgb(15,15,15));   // diamond corner = idx0 -> BG shows

    // --- 16-per-line cap: 20 sprites on one line, expect only the first 16 ---
    std::printf("\nsprite-per-line cap:\n");
    SoftFpga d2;
    loadTiles(d2);
    loadPalette(d2);
    std::vector<TileCell> empty(64 * 64);       // tile0 everywhere -> backdrop
    d2.writeTilemap(0, 0, empty);
    std::vector<OamEntry> many;
    for (int i = 0; i < 20; ++i) many.push_back(sprite(i * 8, 10, false));
    d2.writeOam(many);
    d2.swap();
    VideoFrame g = d2.frame();
    auto at2 = [&](int x, int y) { return g.pixels[y * g.width + x]; };
    // y=14 is inside the sprites; center column of sprite i is x = i*8 + 4.
    check("sprite #0 drawn",  at2(0 * 8 + 4, 14), rgb(15,2,2));  // within cap
    check("sprite #15 drawn", at2(15 * 8 + 4, 14), rgb(15,2,2)); // last allowed
    check("sprite #16 culled",at2(16 * 8 + 4, 14), rgb(1,1,4));  // dropped -> backdrop
    check("sprite #19 culled",at2(19 * 8 + 4, 14), rgb(1,1,4));

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
