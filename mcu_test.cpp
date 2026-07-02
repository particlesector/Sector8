// mcu_test.cpp — headless test of the Lua runtime + API bindings.

#include "soft_fpga.h"
#include "runtime.h"
#include <cstdio>
#include <vector>

using namespace s8;

// same scene assets as the smoke test (host-side asset load, for now)
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
    uint8_t blob[96] = {0};
    packTile(kBox, blob + 32); packTile(kDiamond, blob + 64);
    d.loadBegin(0, sizeof blob);
    d.loadData(std::span<const uint8_t>(blob, sizeof blob));
    d.loadEnd(0); d.bankActivate(0);
    Color12 base[3] = { {1,1,4}, {15,15,15}, {3,12,4} };
    d.setPalette(0, base);
    Color12 sub1[2] = { {0,0,0}, {15,2,2} };
    d.setPalette(16, sub1);
    std::vector<TileCell> map(64 * 64);
    for (auto& c : map) { c.tile = 1; c.palette = 0; }
    d.writeTilemap(0, 0, map);
}

static uint32_t rgb(int r,int g,int b){
    return uint32_t(((r<<4|r)<<24)|((g<<4|g)<<16)|((b<<4|b)<<8)|0xFF);
}
static void writePPM(const char* p, VideoFrame f){
    FILE* fp=std::fopen(p,"wb"); std::fprintf(fp,"P6\n%d %d\n255\n",f.width,f.height);
    for(int i=0;i<f.width*f.height;++i){uint32_t v=f.pixels[i];
        uint8_t c[3]={uint8_t(v>>24),uint8_t(v>>16),uint8_t(v>>8)};std::fwrite(c,1,3,fp);}
    std::fclose(fp);
}

static int failures = 0;
static void check(const char* w, bool ok){
    std::printf("  [%s] %s\n", ok?"PASS":"FAIL", w); if(!ok) ++failures;
}

int main(int argc, char** argv) {
    const char* cart = argc > 1 ? argv[1] : "hello.lua";

    SoftFpga dev;
    buildScene(dev);

    Runtime rt(dev);
    if (!rt.loadCartSource(cart)) { std::printf("cart error: %s\n", rt.lastError()); return 2; }
    rt.callInit();

    // Helper: one frame with a given held button set.
    auto frame = [&](bool right, bool a) {
        InputState in{};
        if (right) in.pad[0].set(Button::Right, true);
        if (a)     in.pad[0].set(Button::A, true);
        rt.runFrame(in);
    };

    // Frame 1: idle. Diamond starts at (156,86); auto-scroll +1.
    frame(false, false);
    VideoFrame f = dev.frame();
    auto at = [&](int x,int y){ return f.pixels[y*f.width+x]; };
    std::printf("Lua runtime assertions:\n");
    check("diamond drawn at start (160,90 red)", at(160,90) == rgb(15,2,2));

    // Hold RIGHT for 20 frames -> px += 2*20 = 40 -> center ~ 200.
    for (int i = 0; i < 20; ++i) frame(true, false);
    f = dev.frame();
    check("sprite moved right (old spot now BG, not red)", at(160,90) != rgb(15,2,2));
    check("sprite present at new spot (~200,90 red)",      at(200,90) == rgb(15,2,2));

    // Camera scrolled: after 21 frames of auto-scroll the BG grid has shifted,
    // so the top-left pixel is no longer guaranteed a tile border. Just confirm
    // the cart ran without error.
    check("no runtime error", rt.lastError()[0] == '\0');

    writePPM("mcu_test.ppm", dev.frame());
    std::printf("\n%s (%d failure%s)\n", failures?"FAILED":"OK",
                failures, failures==1?"":"s");
    return failures ? 1 : 0;
}
