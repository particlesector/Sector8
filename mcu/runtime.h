// mcu/runtime.h
//
// Sector8 MCU runtime: embeds Lua, owns the lua_State + heap arena, registers
// the cart-facing API, and drives _init/_update/_draw. The API bindings turn
// Lua calls into FpgaDevice calls — the same code that will run on ARM (only
// the HAL seams differ). ARM-safe: no exceptions, no RTTI.

#pragma once
#include <array>
#include <cstddef>
#include "fpga_device.h"   // FpgaDevice
#include "input.h"         // InputState / Button

struct lua_State;

namespace s8 {

class Runtime {
public:
    explicit Runtime(FpgaDevice& dev, size_t heapCap = 256 * 1024);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // Load and execute a cart's top-level chunk (defines _init/_update/_draw).
    bool loadCartSource(const char* path);
    // Load a packed .s8 cart: push its assets, then run its bytecode.
    bool loadCart(const char* path);
    const char* title()  const { return title_; }
    const char* author() const { return author_; }
    void callInit();                       // runs _init once
    void runFrame(const InputState& in);   // _update + _draw, flush, SWAP

    const char* lastError() const { return err_; }

    // heap arena (malloc-backed, byte-capped) — firmware swaps in a fixed arena.
    // Public so the file-static lua_Alloc can reach it.
    struct Heap { size_t used = 0; size_t cap = 0; };

    // --- called by the API bindings (public so the C trampolines can reach) ---
    FpgaDevice& fpga() { return dev_; }
    const PadState& pad(int player) const;
    bool pressed(int button, int player) const;   // edge: down now, up last frame
    void pushSprite(const OamEntry& e);
    void setCamera(int16_t x, int16_t y);
    void setLayer(uint8_t layer)   { curLayer_   = layer   < kNumBgLayers ? layer   : 0; }
    uint8_t curLayer() const       { return curLayer_; }
    void setPalette(uint8_t sub)   { curPalette_ = sub     < kSubPalettes ? sub     : 0; }
    uint8_t curPalette() const     { return curPalette_; }

private:
    FpgaDevice& dev_;
    lua_State*  L_ = nullptr;

    Heap heap_;   // uses the public type declared above

    InputState input_{};
    InputState prevInput_{};

    std::array<OamEntry, kMaxSprites> oam_{};
    int      spriteCount_ = 0;
    int16_t  scrollX_[kNumBgLayers] = {0, 0};
    int16_t  scrollY_[kNumBgLayers] = {0, 0};
    uint8_t  curLayer_   = 0;
    uint8_t  curPalette_ = 0;

    char err_[256] = {0};
    char title_[33] = {0};
    char author_[33] = {0};

    void registerApi();
    void callGlobal(const char* name);   // optional global; records errors
    void setError(const char* ctx);
};

} // namespace s8
