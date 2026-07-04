// app/main.cpp  — Sector8 simulator entry point.
//
// The per-frame logic now lives in a Lua cart driven by the runtime. Assets
// (tiles/palette/BG) are still loaded host-side until the cart-asset pipeline
// exists; everything else the cart does through the API bindings.

#include "soft_fpga.h"
#include "host_sdl3.h"
#include "runtime.h"
#include "demo_assets.h"
#include <array>
#include <cstdio>
#include <cstring>

using namespace s8;

static bool endsWith(const char* s, const char* suf) {
    size_t ls = std::strlen(s), lf = std::strlen(suf);
    return ls >= lf && std::strcmp(s + ls - lf, suf) == 0;
}

int main(int argc, char** argv) {
    const char* cart = argc > 1 ? argv[1] : "carts/hello.lua";

    Sdl3Host host;
    if (!host.init("Sector8 \u2014 M1 (Lua)", 1280, 720)) return 1;

    SoftFpga dev;
    Runtime rt(dev);

    bool ok;
    if (endsWith(cart, ".s8")) {
        ok = rt.loadCart(cart);                 // self-contained cart
    } else {
        demo::buildScene(dev);                  // dev path: host-provided assets
        ok = rt.loadCartSource(cart);
    }
    if (!ok) { std::printf("cart error: %s\n", rt.lastError()); host.shutdown(); return 2; }
    rt.callInit();

    std::array<int16_t, 2048> audio{};
    bool running = true;
    while (running) {
        InputState in{};
        running = host.processEvents(in);
        rt.runFrame(in);                 // _update + _draw + SWAP
        host.present(dev.frame());
        int n = dev.pullAudio(audio.data(), kAudioSampleRate / 60);  // ~800 frames/vsync
        host.pushAudio(audio.data(), n);
        host.waitForNextFrame();
    }
    host.shutdown();
    return 0;
}
