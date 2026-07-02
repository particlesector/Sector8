// overlay_test.cpp — headless test of the overlay rasterizer + compositing.

#include "soft_fpga.h"
#include <cstdio>
#include <vector>

using namespace s8;

static void packTile(const int (&px)[8][8], uint8_t out[32]) {
    int o=0; for(int y=0;y<8;++y) for(int x=0;x<8;x+=2) out[o++]=uint8_t((px[y][x]<<4)|px[y][x+1]);
}
static const int kBox[8][8]={
    {1,1,1,1,1,1,1,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},
    {1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,1,1,1,1,1,1,1}};
static void buildScene(SoftFpga& d){
    uint8_t blob[64]={0}; packTile(kBox, blob+32);
    d.loadBegin(0,sizeof blob); d.loadData(std::span<const uint8_t>(blob,sizeof blob));
    d.loadEnd(0); d.bankActivate(0);
    Color12 base[3]={{1,1,4},{15,15,15},{3,12,4}}; d.setPalette(0,base);
    Color12 sub1[2]={{0,0,0},{15,2,2}}; d.setPalette(16,sub1);   // entry17 = red
    std::vector<TileCell> map(64*64); for(auto&c:map){c.tile=1;c.palette=0;}
    d.writeTilemap(0,0,map);
}
static uint32_t rgb(int r,int g,int b){return uint32_t(((r<<4|r)<<24)|((g<<4|g)<<16)|((b<<4|b)<<8)|0xFF);}
static void writePPM(const char*p,VideoFrame f){FILE*fp=std::fopen(p,"wb");
    std::fprintf(fp,"P6\n%d %d\n255\n",f.width,f.height);
    for(int i=0;i<f.width*f.height;++i){uint32_t v=f.pixels[i];
        uint8_t c[3]={uint8_t(v>>24),uint8_t(v>>16),uint8_t(v>>8)};std::fwrite(c,1,3,fp);}
    std::fclose(fp);}
static int failures=0;
static void check(const char*w,bool ok){std::printf("  [%s] %s\n",ok?"PASS":"FAIL",w);if(!ok)++failures;}

int main(){
    SoftFpga dev; buildScene(dev);
    const uint32_t RED = rgb(15,2,2);

    // Overlay resolves through sub-palette 1 -> color index 1 == entry 17 (red).
    dev.setOverlayPalette(1);
    dev.ovlFillRect(10,10,20,20,1);
    dev.ovlRect(40,10,20,20,1);
    dev.ovlLine(70,10,110,30,1);
    dev.ovlCircle(150,20,10,1);
    dev.ovlFillCircle(200,20,8,1);
    dev.ovlText(5,150,1,"SECTOR8 HI 42");
    dev.swap();
    VideoFrame f=dev.frame();
    auto at=[&](int x,int y){return f.pixels[y*f.width+x];};

    std::printf("overlay assertions:\n");
    check("fillRect interior is overlay red", at(15,15)==RED);
    check("fillRect edge is overlay red",     at(10,10)==RED);
    check("undrawn area shows BG (not red)",  at(300,5)!=RED);
    check("fillCircle center is red",         at(200,20)==RED);
    // text region has some red pixels (glyphs rendered)
    bool anyText=false; for(int y=150;y<158&&!anyText;++y) for(int x=5;x<100;++x)
        if(at(x,y)==RED){anyText=true;break;}
    check("text drew glyph pixels", anyText);
    writePPM("overlay_test.ppm", f);

    // Auto-clear: swap again with no draws -> overlay empty, BG shows through.
    dev.swap();
    f=dev.frame();
    check("overlay auto-cleared next frame", at(15,15)!=RED);

    // Mode 1: one logical pixel -> 2x2 physical block.
    SoftFpga d2; buildScene(d2);
    d2.setOverlayPalette(1);
    d2.setOverlayMode(OverlayMode::Pure160x90);
    d2.ovlPixel(5,5,1);              // logical (5,5) -> physical (10,10)..(11,11)
    d2.swap();
    VideoFrame g=d2.frame();
    auto at2=[&](int x,int y){return g.pixels[y*g.width+x];};
    check("mode1 pixel doubled: (10,10) red",  at2(10,10)==RED);
    check("mode1 pixel doubled: (11,11) red",  at2(11,11)==RED);
    check("mode1 pixel not at (12,10)",        at2(12,10)!=RED);

    std::printf("\n%s (%d failure%s)\n", failures?"FAILED":"OK", failures, failures==1?"":"s");
    return failures?1:0;
}
