// app/main.cpp  — Sector8 simulator entry point.
//
// The per-frame logic now lives in a Lua cart driven by the runtime. Assets
// (tiles/palette/BG) are still loaded host-side until the cart-asset pipeline
// exists; everything else the cart does through the API bindings.

#include "soft_fpga.h"
#include "host_sdl3.h"
#include "runtime.h"
#include <array>
#include <cstdio>
#include <vector>

using namespace s8;

// --- host-side scene assets (temporary; becomes cart data later) -----------
static void packTile(const int (&px)[8][8], uint8_t out[32]) {
    int o = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; x += 2)
            out[o++] = uint8_t((px[y][x] << 4) | px[y][x + 1]);
}
static const int kBox[8][8] = {
    {1,1,1,1,1,1,1,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},
    {1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,1,1,1,1,1,1,1},
};
static const int kDiamond[8][8] = {
    {0,0,0,1,1,0,0,0},{0,0,1,1,1,1,0,0},{0,1,1,1,1,1,1,0},{1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},{0,1,1,1,1,1,1,0},{0,0,1,1,1,1,0,0},{0,0,0,1,1,0,0,0},
};
static void buildScene(SoftFpga& d) {
    uint8_t blob[128] = {0};
    packTile(kBox, blob + 32);                       // tile 1
    packTile(kDiamond, blob + 64);                   // tile 2
    for (int i = 96; i < 128; ++i) blob[i] = 0x33;   // tile 3 = solid index 3 (pillar)
    d.loadBegin(0, sizeof blob);
    d.loadData(std::span<const uint8_t>(blob, sizeof blob));
    d.loadEnd(0); d.bankActivate(0);

    Color12 base[4] = { {1,1,4}, {15,15,15}, {3,12,4}, {15,9,2} };  // navy/white/green/orange
    d.setPalette(0, base);
    Color12 sub1[2] = { {0,0,0}, {15,2,2} };         // entry 17 = red (sprite + HUD)
    d.setPalette(16, sub1);

    // BG0: box grid everywhere.
    std::vector<TileCell> bg0(64 * 64);
    for (auto& c : bg0) { c.tile = 1; c.palette = 0; }
    d.writeTilemap(0, 0, bg0);

    // BG1: orange foreground pillars, priority set, so the player passes behind
    // them. Scrolled at 2x by the cart for parallax.
    std::vector<TileCell> bg1(64 * 64);              // default tile 0 = transparent
    for (int col = 3; col < 64; col += 8)
        for (int row = 6; row <= 15; ++row) {
            TileCell& c = bg1[row * 64 + col];
            c.tile = 3; c.palette = 0; c.priority = true;
        }
    d.writeTilemap(1, 0, bg1);
}

int main(int argc, char** argv) {
    const char* cart = argc > 1 ? argv[1] : "carts/hello.lua";

    Sdl3Host host;
    if (!host.init("Sector8 \u2014 M1 (Lua)", 1280, 720)) return 1;

    SoftFpga dev;
    buildScene(dev);

    Runtime rt(dev);
    if (!rt.loadCartSource(cart)) {
        std::printf("cart error: %s\n", rt.lastError());
        host.shutdown();
        return 2;
    }
    rt.callInit();

    std::array<int16_t, 2048> audio{};
    bool running = true;
    while (running) {
        InputState in{};
        running = host.processEvents(in);
        rt.runFrame(in);                 // _update + _draw + SWAP
        host.present(dev.frame());
        int n = dev.pullAudio(audio.data(), int(audio.size() / 2));
        host.pushAudio(audio.data(), n);
        host.waitForNextFrame();
    }
    host.shutdown();
    return 0;
}
