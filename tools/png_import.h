// tools/png_import.h — PNG spritesheet -> Sector8 palette + tiles (host-side).
//
// Auto-extracts a 16-color sub-palette (index 0 = transparent) from a PNG whose
// width/height are multiples of 8, and slices it into 8x8 4bpp tiles in grid
// (row-major) order. alpha < 128 -> transparent. Colors quantize to 12-bit.

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "s8_types.h"     // Color12, kWavetableLen etc.

namespace s8 {

struct PalEntry { uint8_t index; Color12 color; };

struct ImportedSheet {
    std::vector<uint8_t> tiles;      // tileCount * 32 bytes (4bpp)
    std::vector<PalEntry> palette;   // index 0 (transparent/backdrop) .. N
    int tileCount = 0;
    int tilesX = 0, tilesY = 0;
};

// Returns false and sets `err` on failure (bad file, non-multiple-of-8, >15 colors).
bool importSpritesheet(const char* pngPath, ImportedSheet& out, std::string& err);

} // namespace s8
