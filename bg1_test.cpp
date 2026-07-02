// bg1_test.cpp — BG1 parallax + per-cell priority compositing.

#include "soft_fpga.h"
#include <cstdio>
#include <vector>

using namespace s8;

static uint32_t rgb(int r,int g,int b){return uint32_t(((r<<4|r)<<24)|((g<<4|g)<<16)|((b<<4|b)<<8)|0xFF);}
static void writePPM(const char*p,VideoFrame f){FILE*fp=std::fopen(p,"wb");
    std::fprintf(fp,"P6\n%d %d\n255\n",f.width,f.height);
    for(int i=0;i<f.width*f.height;++i){uint32_t v=f.pixels[i];
        uint8_t c[3]={uint8_t(v>>24),uint8_t(v>>16),uint8_t(v>>8)};std::fwrite(c,1,3,fp);}
    std::fclose(fp);}
static int failures=0;
static void check(const char*w,bool ok){std::printf("  [%s] %s\n",ok?"PASS":"FAIL",w);if(!ok)++failures;}

int main(){
    const uint32_t BLUE=rgb(2,4,15), GREEN=rgb(3,12,4), RED=rgb(15,2,2);

    SoftFpga dev;
    // tiles: 0 blank, 1 solid idx1, 2 solid idx2
    uint8_t blob[96]={0};
    for(int i=32;i<64;++i) blob[i]=0x11;   // tile1 solid index 1
    for(int i=64;i<96;++i) blob[i]=0x22;   // tile2 solid index 2
    dev.loadBegin(0,sizeof blob); dev.loadData(std::span<const uint8_t>(blob,sizeof blob));
    dev.loadEnd(0); dev.bankActivate(0);

    Color12 p0[3]={{1,1,4},{2,4,15},{3,12,4}};  // navy / blue / green
    dev.setPalette(0,p0);
    Color12 p1[2]={{0,0,0},{15,2,2}};           // entry17 = red
    dev.setPalette(16,p1);

    // BG0: entire map = tile1 (blue), palette 0.
    std::vector<TileCell> bg0(64*64);
    for(auto&c:bg0){c.tile=1;c.palette=0;}
    dev.writeTilemap(0,0,bg0);

    // BG1: row 5 (y 40..47) -> tile2 (green). cols 0..9 normal, cols 10..19 priority.
    std::vector<TileCell> band(20);
    for(int i=0;i<20;++i){band[i].tile=2;band[i].palette=0;band[i].priority=(i>=10);}
    dev.writeTilemap(1, 5*64, band);

    // Sprites (tile1 solid, palette 1 -> red), both on the band (y=40).
    std::vector<OamEntry> oam;
    auto sp=[&](int x){OamEntry e{};e.x=int16_t(x);e.y=40;e.tile=1;e.palette=1;e.enabled=true;return e;};
    oam.push_back(sp(20));    // over non-priority BG1 (col ~2)
    oam.push_back(sp(100));   // over priority BG1     (col ~12)
    dev.writeOam(oam);

    // Overlay pixel over a priority BG cell, away from sprites.
    dev.setOverlayPalette(0);
    dev.ovlPixel(150, 43, 1);   // color 1 -> blue, must beat priority-green

    dev.swap();
    VideoFrame f=dev.frame();
    auto at=[&](int x,int y){return f.pixels[y*f.width+x];};

    std::printf("BG1 + priority assertions:\n");
    check("BG0 shows where BG1 transparent (blue)", at(5,100)==BLUE);
    check("BG1 draws over BG0 (green)",             at(5,43)==GREEN);
    check("sprite over non-priority BG1 (red)",     at(23,43)==RED);
    check("priority BG1 stays above sprite (green)",at(103,43)==GREEN);
    check("overlay beats priority BG (blue)",       at(150,43)==BLUE);

    writePPM("bg1_test.ppm", f);
    std::printf("\n%s (%d failure%s)\n", failures?"FAILED":"OK", failures, failures==1?"":"s");
    return failures?1:0;
}
