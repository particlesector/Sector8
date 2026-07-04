// app/demo_assets.h
//
// The demo scene's assets in one place, so the .lua dev path (pushes to a
// device), the packer (writes cart sections), and the round-trip test (builds a
// reference) all use identical data. This is the scaffold the PNG importer will
// replace once carts carry their own art.

#pragma once
#include <cstdint>
#include <vector>
#include "fpga_device.h"

namespace s8::demo {

inline void packTile(const int (&px)[8][8], uint8_t out[32]) {
    int o = 0;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; x += 2)
            out[o++] = uint8_t((px[y][x] << 4) | px[y][x + 1]);
}
inline const int kBox[8][8] = {
    {1,1,1,1,1,1,1,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},
    {1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,2,2,2,2,2,2,1},{1,1,1,1,1,1,1,1},
};
inline const int kDiamond[8][8] = {
    {0,0,0,1,1,0,0,0},{0,0,1,1,1,1,0,0},{0,1,1,1,1,1,1,0},{1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1},{0,1,1,1,1,1,1,0},{0,0,1,1,1,1,0,0},{0,0,0,1,1,0,0,0},
};

// 128-byte tile bank: tile0 blank, 1 box, 2 diamond, 3 solid pillar (index 3).
inline std::vector<uint8_t> tiles() {
    std::vector<uint8_t> b(128, 0);
    packTile(kBox, b.data() + 32);
    packTile(kDiamond, b.data() + 64);
    for (int i = 96; i < 128; ++i) b[i] = 0x33;
    return b;
}
// Palette entries as (index, Color12). Non-contiguous, so indices are explicit.
struct PalEntry { uint8_t index; Color12 color; };
inline std::vector<PalEntry> palette() {
    return { {0,{1,1,4}}, {1,{15,15,15}}, {2,{3,12,4}}, {3,{15,9,2}}, {17,{15,2,2}} };
}
inline std::vector<TileCell> bg0() {           // box grid everywhere
    std::vector<TileCell> m(64 * 64);
    for (auto& c : m) { c.tile = 1; c.palette = 0; }
    return m;
}
inline std::vector<TileCell> bg1() {           // orange priority pillars
    std::vector<TileCell> m(64 * 64);
    for (int col = 3; col < 64; col += 8)
        for (int row = 6; row <= 15; ++row) {
            auto& c = m[row * 64 + col];
            c.tile = 3; c.palette = 0; c.priority = true;
        }
    return m;
}

// Push the whole scene to a device (used by the .lua dev path and the test ref).
inline void buildScene(FpgaDevice& d) {
    const auto t = tiles();
    d.loadBegin(0, uint32_t(t.size()));
    d.loadData(std::span<const uint8_t>(t.data(), t.size()));
    d.loadEnd(0); d.bankActivate(0);
    for (const auto& e : palette()) {
        Color12 c = e.color;
        d.setPalette(e.index, std::span<const Color12>(&c, 1));
    }
    auto m0 = bg0(); d.writeTilemap(0, 0, m0);
    auto m1 = bg1(); d.writeTilemap(1, 0, m1);
}

} // namespace s8::demo
