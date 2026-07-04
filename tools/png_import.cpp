// tools/png_import.cpp — implementation of the PNG spritesheet importer.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#include "png_import.h"
#include <cstdio>
#include <map>

namespace s8 {

// 8-bit channel -> 4-bit (12-bit gamut) with rounding.
static uint8_t to4(uint8_t c) { return uint8_t((int(c) * 15 + 127) / 255); }

bool importSpritesheet(const char* pngPath, ImportedSheet& out, std::string& err) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* px = stbi_load(pngPath, &w, &h, &comp, 4);   // force RGBA
    if (!px) { err = std::string("cannot decode PNG: ") + stbi_failure_reason(); return false; }

    if (w % 8 != 0 || h % 8 != 0) {
        stbi_image_free(px);
        err = "image dimensions must be multiples of 8";
        return false;
    }

    // Build the palette (index 0 = transparent) and a per-pixel index map.
    std::vector<uint8_t> idx(size_t(w) * h, 0);
    std::map<uint32_t, uint8_t> colorToIndex;       // packed 12-bit rgb -> index
    out.palette.clear();
    out.palette.push_back({ 0, {0, 0, 0} });        // index 0: transparent / backdrop

    for (int i = 0; i < w * h; ++i) {
        const stbi_uc* p = px + size_t(i) * 4;
        if (p[3] < 128) { idx[i] = 0; continue; }   // transparent
        const uint8_t r = to4(p[0]), g = to4(p[1]), b = to4(p[2]);
        const uint32_t key = (uint32_t(r) << 8) | (uint32_t(g) << 4) | b;
        auto it = colorToIndex.find(key);
        if (it == colorToIndex.end()) {
            if (int(out.palette.size()) >= 16) {     // 15 colors + transparent
                stbi_image_free(px);
                err = "too many colors: max 15 opaque + transparent";
                return false;
            }
            const uint8_t newIdx = uint8_t(out.palette.size());
            colorToIndex[key] = newIdx;
            out.palette.push_back({ newIdx, { r, g, b } });
            idx[i] = newIdx;
        } else {
            idx[i] = it->second;
        }
    }

    // Slice into 8x8 tiles, grid (row-major) order, packed 4bpp (left = high nibble).
    out.tilesX = w / 8; out.tilesY = h / 8;
    out.tileCount = out.tilesX * out.tilesY;
    out.tiles.assign(size_t(out.tileCount) * 32, 0);
    for (int ty = 0; ty < out.tilesY; ++ty)
        for (int tx = 0; tx < out.tilesX; ++tx) {
            uint8_t* t = out.tiles.data() + size_t(ty * out.tilesX + tx) * 32;
            int o = 0;
            for (int row = 0; row < 8; ++row)
                for (int col = 0; col < 8; col += 2) {
                    const int px0 = idx[size_t((ty*8+row))*w + (tx*8+col)];
                    const int px1 = idx[size_t((ty*8+row))*w + (tx*8+col+1)];
                    t[o++] = uint8_t((px0 << 4) | px1);
                }
        }

    stbi_image_free(px);
    return true;
}

} // namespace s8
