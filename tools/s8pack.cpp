// tools/s8pack.cpp — Sector8 cart packer.
//
//   s8pack <in.lua> <out.s8> [--tiles sheet.png] [--title T] [--author A]
//
// Bytecode comes from the input .lua (dumped with the pinned 32-bit config).
// If --tiles is given, a PNG spritesheet is imported into PALETTE + TILES
// sections; otherwise the cart carries code only (graphics come from the cart's
// own Lua, e.g. set_tile / sprite).

#include "cart.h"
#include "png_import.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

using namespace s8;

struct CartBuilder {
    struct Sec { uint16_t type; std::vector<uint8_t> data; };
    std::vector<Sec> secs;
    void add(CartSection t, std::vector<uint8_t> d) { secs.push_back({ uint16_t(t), std::move(d) }); }
    static void put16(std::vector<uint8_t>& v, uint16_t x){ v.push_back(uint8_t(x)); v.push_back(uint8_t(x>>8)); }
    static void put32(std::vector<uint8_t>& v, uint32_t x){ for(int i=0;i<4;++i) v.push_back(uint8_t(x>>(8*i))); }
    std::vector<uint8_t> bytes() const {
        std::vector<uint8_t> out;
        out.insert(out.end(), kCartMagic, kCartMagic + 4);
        put16(out, kCartFormatVersion); put16(out, 1);
        put16(out, uint16_t(secs.size())); put16(out, 0);
        for (const auto& s : secs) {
            put16(out, s.type); put16(out, 0);
            put32(out, uint32_t(s.data.size()));
            put32(out, crc32(s.data.data(), s.data.size()));
            out.insert(out.end(), s.data.begin(), s.data.end());
        }
        return out;
    }
};

static std::vector<uint8_t> compileLua(const char* path, std::string& err) {
    lua_State* L = luaL_newstate();
    std::vector<uint8_t> bc;
    if (luaL_loadfilex(L, path, "t") != LUA_OK) {
        err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "compile error";
        lua_close(L); return {};
    }
    auto writer = [](lua_State*, const void* p, size_t sz, void* ud) -> int {
        auto* v = static_cast<std::vector<uint8_t>*>(ud);
        const uint8_t* b = static_cast<const uint8_t*>(p);
        v->insert(v->end(), b, b + sz); return 0;
    };
    lua_dump(L, writer, &bc, 0);
    lua_close(L);
    return bc;
}

static std::vector<uint8_t> metaSection(const char* title, const char* author) {
    std::vector<uint8_t> m(64, 0);
    std::strncpy(reinterpret_cast<char*>(m.data()),      title,  32);
    std::strncpy(reinterpret_cast<char*>(m.data() + 32), author, 32);
    return m;
}
static std::vector<uint8_t> paletteSection(const ImportedSheet& s) {
    std::vector<uint8_t> v;
    for (const auto& e : s.palette) { v.push_back(e.index);
        v.push_back(e.color.r); v.push_back(e.color.g); v.push_back(e.color.b); }
    return v;
}

int main(int argc, char** argv) {
    const char* in = nullptr; const char* out = nullptr;
    const char* tiles = nullptr; const char* title = "Untitled"; const char* author = "anonymous";
    std::vector<const char*> pos;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--tiles"  && i+1 < argc) tiles  = argv[++i];
        else if (a == "--title"  && i+1 < argc) title  = argv[++i];
        else if (a == "--author" && i+1 < argc) author = argv[++i];
        else pos.push_back(argv[i]);
    }
    if (pos.size() < 2) {
        std::fprintf(stderr, "usage: s8pack <in.lua> <out.s8> [--tiles sheet.png] [--title T] [--author A]\n");
        return 2;
    }
    in = pos[0]; out = pos[1];

    std::string err;
    auto bc = compileLua(in, err);
    if (bc.empty()) { std::fprintf(stderr, "s8pack: %s\n", err.c_str()); return 1; }

    CartBuilder cb;
    cb.add(CartSection::Meta,     metaSection(title, author));
    cb.add(CartSection::Bytecode, bc);

    int tileCount = 0;
    if (tiles) {
        ImportedSheet sheet;
        if (!importSpritesheet(tiles, sheet, err)) { std::fprintf(stderr, "s8pack: %s\n", err.c_str()); return 1; }
        cb.add(CartSection::Palette, paletteSection(sheet));
        cb.add(CartSection::Tiles,   sheet.tiles);
        tileCount = sheet.tileCount;
    }

    auto blob = cb.bytes();
    FILE* fp = std::fopen(out, "wb");
    if (!fp) { std::fprintf(stderr, "s8pack: cannot write %s\n", out); return 1; }
    std::fwrite(blob.data(), 1, blob.size(), fp);
    std::fclose(fp);
    std::printf("packed %s -> %s (%zu bytes, bytecode %zu, tiles %d)\n",
                in, out, blob.size(), bc.size(), tileCount);
    return 0;
}
