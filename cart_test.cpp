// cart_test.cpp — end-to-end: a PNG-imported cart loads and displays correctly.
//   (build s8pack first, then: s8pack example.lua example.s8 --tiles sheet.png ...)

#include "soft_fpga.h"
#include "runtime.h"
#include <cstdio>
#include <cstring>

using namespace s8;

static int failures = 0;
static void check(const char* w, bool ok) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++failures;
}

int main(int argc, char** argv) {
    const char* cart = argc > 1 ? argv[1] : "example.s8";
    const uint32_t RED  = 0xFF0000FFu;   // imported (15,0,0)
    const uint32_t BLUE = 0x0000FFFFu;   // imported (0,0,15)

    SoftFpga dev;
    Runtime rt(dev);
    if (!rt.loadCart(cart)) { std::printf("cart: %s\n", rt.lastError()); return 2; }
    rt.callInit();                       // fills BG from imported tiles

    InputState idle{};
    for (int i = 0; i < 3; ++i) rt.runFrame(idle);
    VideoFrame f = dev.frame();
    auto at = [&](int x, int y){ return f.pixels[y * f.width + x]; };

    std::printf("PNG cart end-to-end assertions:\n");
    check("metadata parsed", std::strlen(rt.title()) > 0);
    std::printf("       title=\"%s\" author=\"%s\"\n", rt.title(), rt.author());
    check("BG shows imported red tile",   at(10, 10) == RED);
    check("sprite shows imported blue tile", at(159, 89) == BLUE);

    std::printf("\n%s (%d failure%s)\n", failures?"FAILED":"OK", failures, failures==1?"":"s");
    return failures ? 1 : 0;
}
