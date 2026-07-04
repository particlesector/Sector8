// png_import_test.cpp — verify the spritesheet importer against a known PNG.
//   (sheet.png: 16x16 -> tile0 red, tile1 green, tile2 blue, tile3 transparent)

#include "png_import.h"
#include <cstdio>

using namespace s8;

static int failures = 0;
static void check(const char* w, bool ok) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", w); if (!ok) ++failures;
}
static bool tileAll(const ImportedSheet& s, int tile, uint8_t byte) {
    const uint8_t* t = s.tiles.data() + size_t(tile) * 32;
    for (int i = 0; i < 32; ++i) if (t[i] != byte) return false;
    return true;
}
static bool col(const PalEntry& e, int r, int g, int b) {
    return e.color.r == r && e.color.g == g && e.color.b == b;
}

int main(int argc, char** argv) {
    const char* png = argc > 1 ? argv[1] : "sheet.png";
    ImportedSheet s; std::string err;
    if (!importSpritesheet(png, s, err)) { std::printf("import failed: %s\n", err.c_str()); return 2; }

    std::printf("importer assertions:\n");
    check("2x2 tile grid",           s.tilesX == 2 && s.tilesY == 2 && s.tileCount == 4);
    check("palette = 3 colors + transparent", s.palette.size() == 4);
    check("index 0 is transparent slot",      s.palette[0].index == 0);
    check("index 1 = red (15,0,0)",   col(s.palette[1], 15, 0, 0));
    check("index 2 = green (0,15,0)", col(s.palette[2], 0, 15, 0));
    check("index 3 = blue (0,0,15)",  col(s.palette[3], 0, 0, 15));
    check("tile0 solid index 1 (0x11)", tileAll(s, 0, 0x11));
    check("tile1 solid index 2 (0x22)", tileAll(s, 1, 0x22));
    check("tile2 solid index 3 (0x33)", tileAll(s, 2, 0x33));
    check("tile3 transparent (0x00)",   tileAll(s, 3, 0x00));

    std::printf("\n%s (%d failure%s)\n", failures?"FAILED":"OK", failures, failures==1?"":"s");
    return failures ? 1 : 0;
}
