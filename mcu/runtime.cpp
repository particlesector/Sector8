// mcu/runtime.cpp  — Sector8 MCU runtime + Lua API bindings.

#include "runtime.h"
#include "cart.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace s8 {

// ---- arena allocator: malloc-backed, byte-capped (models the 256 KB heap) --
static void* s8_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* h = static_cast<Runtime::Heap*>(ud);
    const size_t old = ptr ? osize : 0;   // osize is a type tag when ptr==NULL
    if (nsize == 0) { std::free(ptr); h->used -= old; return nullptr; }
    if (h->used - old + nsize > h->cap) return nullptr;   // over budget -> Lua errors
    void* np = std::realloc(ptr, nsize);
    if (np) h->used = h->used - old + nsize;
    return np;
}

static Runtime* self(lua_State* L) {
    return *static_cast<Runtime**>(lua_getextraspace(L));
}

// ---- API bindings ----------------------------------------------------------
// sprite(id, x, y, [flip])   flip bit0 = H, bit1 = V. Sub-palette = current.
static int l_sprite(lua_State* L) {
    Runtime* rt = self(L);
    OamEntry e{};
    e.tile    = uint16_t(luaL_checkinteger(L, 1));
    e.x       = int16_t (luaL_checkinteger(L, 2));
    e.y       = int16_t (luaL_checkinteger(L, 3));
    const int flip = int(luaL_optinteger(L, 4, 0));
    e.flip_h  = flip & 1;
    e.flip_v  = (flip >> 1) & 1;
    e.palette = rt->curPalette();
    e.enabled = true;
    rt->pushSprite(e);
    return 0;
}

static int l_camera(lua_State* L) {
    self(L)->setCamera(int16_t(luaL_optinteger(L, 1, 0)),
                       int16_t(luaL_optinteger(L, 2, 0)));
    return 0;
}

// set_tile(cx, cy, tile, [palette], [flags])  -> writes one cell of the active layer.
static int l_set_tile(lua_State* L) {
    const int cx = int(luaL_checkinteger(L, 1));
    const int cy = int(luaL_checkinteger(L, 2));
    if (cx < 0 || cx >= kBgMapW || cy < 0 || cy >= kBgMapH) return 0;
    const int flip = int(luaL_optinteger(L, 5, 0));
    TileCell c{ uint16_t(luaL_checkinteger(L, 3)), uint8_t(luaL_optinteger(L, 4, 0)),
               bool(flip & 1), bool(flip & 2), bool(flip & 4) };
    Runtime* rt = self(L);
    rt->fpga().writeTilemap(rt->curLayer(), uint16_t(cy * kBgMapW + cx),
                            std::span<const TileCell>(&c, 1));
    return 0;
}

static int l_layer(lua_State* L) {
    self(L)->setLayer(uint8_t(luaL_checkinteger(L, 1)));
    return 0;
}

static int l_use_palette(lua_State* L) {
    self(L)->setPalette(uint8_t(luaL_checkinteger(L, 1)));
    return 0;
}

static int l_button(lua_State* L) {
    Runtime* rt = self(L);
    const int b = int(luaL_checkinteger(L, 1));
    const int p = int(luaL_optinteger(L, 2, 0));
    bool held = false;
    if (p >= 0 && p < kMaxPlayers && b >= 0 && b < int(Button::Count))
        held = rt->pad(p).down(Button(b));
    lua_pushboolean(L, held);
    return 1;
}

static int l_button_pressed(lua_State* L) {
    Runtime* rt = self(L);
    const int b = int(luaL_checkinteger(L, 1));
    const int p = int(luaL_optinteger(L, 2, 0));
    lua_pushboolean(L, rt->pressed(b, p));
    return 1;
}

// ---- overlay bindings (command-stream: dispatched immediately) -------------
static int l_pset(lua_State* L) {
    self(L)->fpga().ovlPixel(int(luaL_checkinteger(L,1)), int(luaL_checkinteger(L,2)),
                             uint8_t(luaL_checkinteger(L,3)));
    return 0;
}
static int l_line(lua_State* L) {
    self(L)->fpga().ovlLine(int(luaL_checkinteger(L,1)), int(luaL_checkinteger(L,2)),
                            int(luaL_checkinteger(L,3)), int(luaL_checkinteger(L,4)),
                            uint8_t(luaL_checkinteger(L,5)));
    return 0;
}
// rect/rect_fill take corners (x0,y0,x1,y1,c), PICO-8 style.
static int l_rect(lua_State* L) {
    int x0=int(luaL_checkinteger(L,1)), y0=int(luaL_checkinteger(L,2));
    int x1=int(luaL_checkinteger(L,3)), y1=int(luaL_checkinteger(L,4));
    self(L)->fpga().ovlRect(x0, y0, x1-x0+1, y1-y0+1, uint8_t(luaL_checkinteger(L,5)));
    return 0;
}
static int l_rectfill(lua_State* L) {
    int x0=int(luaL_checkinteger(L,1)), y0=int(luaL_checkinteger(L,2));
    int x1=int(luaL_checkinteger(L,3)), y1=int(luaL_checkinteger(L,4));
    self(L)->fpga().ovlFillRect(x0, y0, x1-x0+1, y1-y0+1, uint8_t(luaL_checkinteger(L,5)));
    return 0;
}
static int l_circle(lua_State* L) {
    self(L)->fpga().ovlCircle(int(luaL_checkinteger(L,1)), int(luaL_checkinteger(L,2)),
                              int(luaL_checkinteger(L,3)), uint8_t(luaL_checkinteger(L,4)));
    return 0;
}
static int l_circfill(lua_State* L) {
    self(L)->fpga().ovlFillCircle(int(luaL_checkinteger(L,1)), int(luaL_checkinteger(L,2)),
                                  int(luaL_checkinteger(L,3)), uint8_t(luaL_checkinteger(L,4)));
    return 0;
}
// text(str, x, y, c)  (print)
static int l_text(lua_State* L) {
    size_t n = 0; const char* s = luaL_checklstring(L, 1, &n);
    self(L)->fpga().ovlText(int(luaL_checkinteger(L,2)), int(luaL_checkinteger(L,3)),
                            uint8_t(luaL_checkinteger(L,4)), std::string_view(s, n));
    return 0;
}
static int l_overlay_mode(lua_State* L) {
    self(L)->fpga().setOverlayMode(luaL_checkinteger(L,1) ? OverlayMode::Pure160x90
                                                          : OverlayMode::Native320x180);
    return 0;
}
static int l_overlay_palette(lua_State* L) {
    self(L)->fpga().setOverlayPalette(uint8_t(luaL_checkinteger(L,1)));
    return 0;
}
// clip() resets to full; clip(x,y,w,h) bounds the region.
static int l_clip(lua_State* L) {
    if (lua_gettop(L) == 0) { self(L)->fpga().ovlClip(0, 0, kScreenW, kScreenH); return 0; }
    self(L)->fpga().ovlClip(int(luaL_checkinteger(L,1)), int(luaL_checkinteger(L,2)),
                            int(luaL_checkinteger(L,3)), int(luaL_checkinteger(L,4)));
    return 0;
}
static int l_blit(lua_State* L) {
    int x=int(luaL_checkinteger(L,1)), y=int(luaL_checkinteger(L,2));
    int w=int(luaL_checkinteger(L,3)), h=int(luaL_checkinteger(L,4));
    size_t n=0; const char* p = luaL_checklstring(L, 5, &n);
    self(L)->fpga().ovlBlit(x, y, w, h,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(p), n));
    return 0;
}

// ---- audio bindings --------------------------------------------------------
// sound(channel, freq_hz, waveform, volume)  waveform: 0..7 slot, 8 = noise.
static int l_sound(lua_State* L) {
    self(L)->fpga().audioSetChannel(uint8_t(luaL_checkinteger(L,1)),
                                    uint16_t(luaL_checkinteger(L,2)),
                                    uint8_t(luaL_checkinteger(L,3)),
                                    uint8_t(luaL_checkinteger(L,4)));
    return 0;
}
static int l_sound_off(lua_State* L) {
    self(L)->fpga().audioNoteOff(uint8_t(luaL_checkinteger(L,1)));
    return 0;
}
// set_wavetable(slot, {32 samples in -128..127})
static int l_set_wavetable(lua_State* L) {
    const int slot = int(luaL_checkinteger(L,1));
    luaL_checktype(L, 2, LUA_TTABLE);
    int8_t buf[kWavetableLen];
    for (int i = 0; i < kWavetableLen; ++i) {
        lua_geti(L, 2, i + 1);
        buf[i] = int8_t(std::clamp(int(luaL_optinteger(L, -1, 0)), -128, 127));
        lua_pop(L, 1);
    }
    self(L)->fpga().audioLoadWavetable(uint8_t(slot),
        std::span<const int8_t>(buf, kWavetableLen));
    return 0;
}
static int l_music(lua_State* L) {   // Part 2b: sequencer
    self(L)->fpga().music(int(luaL_checkinteger(L,1)), int(luaL_optinteger(L,2,0)));
    return 0;
}

// set_instrument(id, {wave=SINE, a=, d=, s=, r=})
static int l_set_instrument(lua_State* L) {
    const int id = int(luaL_checkinteger(L,1));
    luaL_checktype(L, 2, LUA_TTABLE);
    auto field = [&](const char* k, int def) {
        lua_getfield(L, 2, k);
        int v = int(luaL_optinteger(L, -1, def));
        lua_pop(L, 1); return v;
    };
    self(L)->fpga().audioSetInstrument(uint8_t(id),
        uint8_t(field("wave",0)), uint8_t(field("a",0)), uint8_t(field("d",0)),
        uint8_t(field("s",255)),  uint8_t(field("r",0)));
    return 0;
}

// set_sfx(id, {speed=8, notes={ {note=,inst=,vol=}, ... }})
static int l_set_sfx(lua_State* L) {
    const int id = int(luaL_checkinteger(L,1));
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_getfield(L, 2, "speed");
    const int speed = int(luaL_optinteger(L, -1, 8));
    lua_pop(L, 1);

    SfxStep steps[kSfxSteps] = {};
    lua_getfield(L, 2, "notes");
    if (lua_istable(L, -1)) {
        for (int i = 0; i < kSfxSteps; ++i) {
            lua_geti(L, -1, i + 1);              // notes[i+1]
            if (lua_istable(L, -1)) {
                auto f = [&](const char* k, int def) {
                    lua_getfield(L, -1, k);
                    int v = int(luaL_optinteger(L, -1, def));
                    lua_pop(L, 1); return v;
                };
                steps[i] = { uint8_t(f("note",0)), uint8_t(f("inst",0)), uint8_t(f("vol",0)) };
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    self(L)->fpga().audioSetSfx(uint8_t(id), uint8_t(speed),
        std::span<const SfxStep>(steps, kSfxSteps));
    return 0;
}

// sfx(id, [channel])  -- channel omitted = auto-pick (steal quietest if busy)
// set_music(id, {sfx={0,1,2,-1}, loop_start=, loop_end=, stop=})
static int l_set_music(lua_State* L) {
    const int id = int(luaL_checkinteger(L,1));
    luaL_checktype(L, 2, LUA_TTABLE);
    uint8_t sfx[kAudioChannels] = {0xFF, 0xFF, 0xFF, 0xFF};
    lua_getfield(L, 2, "sfx");
    if (lua_istable(L, -1)) {
        for (int i = 0; i < kAudioChannels; ++i) {
            lua_geti(L, -1, i + 1);
            int v = int(luaL_optinteger(L, -1, -1));
            sfx[i] = (v < 0) ? 0xFF : uint8_t(v);
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    auto flag = [&](const char* k) {
        lua_getfield(L, 2, k); bool b = lua_toboolean(L, -1); lua_pop(L, 1); return b;
    };
    const uint8_t flags = uint8_t((flag("loop_start")?1:0) | (flag("loop_end")?2:0)
                                | (flag("stop")?4:0));
    self(L)->fpga().audioSetMusic(uint8_t(id), std::span<const uint8_t>(sfx, kAudioChannels), flags);
    return 0;
}

static int l_play_sfx(lua_State* L) {
    self(L)->fpga().audioPlaySfx(uint8_t(luaL_checkinteger(L,1)),
                                 int(luaL_optinteger(L,2,-1)));
    return 0;
}

void Runtime::registerApi() {
    const luaL_Reg api[] = {
        {"sprite", l_sprite}, {"spr", l_sprite},
        {"camera", l_camera},
        {"set_tile", l_set_tile}, {"mset", l_set_tile},
        {"layer",  l_layer},
        {"use_palette", l_use_palette},
        {"button", l_button}, {"btn", l_button},
        {"button_pressed", l_button_pressed}, {"btnp", l_button_pressed},
        {"set_pixel", l_pset}, {"pset", l_pset},
        {"line", l_line},
        {"rect", l_rect}, {"rect_fill", l_rectfill}, {"rectfill", l_rectfill},
        {"circle", l_circle}, {"circ", l_circle},
        {"circle_fill", l_circfill}, {"circfill", l_circfill},
        {"text", l_text}, {"print", l_text},
        {"overlay_mode", l_overlay_mode}, {"overlay_palette", l_overlay_palette},
        {"clip", l_clip}, {"blit", l_blit},
        {"sound", l_sound},
        {"sound_off", l_sound_off},
        {"set_wavetable", l_set_wavetable},
        {"set_instrument", l_set_instrument},
        {"set_sfx", l_set_sfx},
        {"set_music", l_set_music},
        {"sfx", l_play_sfx},
        {"music", l_music},
        {nullptr, nullptr},
    };
    for (const luaL_Reg* r = api; r->name; ++r) {
        lua_pushcfunction(L_, r->func);
        lua_setglobal(L_, r->name);
    }
    // Button constants as globals: LEFT, RIGHT, UP, DOWN, A, B, X, Y, START, SELECT
    const char* names[] = {"LEFT","RIGHT","UP","DOWN","A","B","X","Y","START","SELECT"};
    for (int i = 0; i < int(Button::Count); ++i) {
        lua_pushinteger(L_, i);
        lua_setglobal(L_, names[i]);
    }
    // Waveform constants.
    const char* waves[] = {"SQUARE","TRIANGLE","SAW","SINE"};
    for (int i = 0; i < 4; ++i) { lua_pushinteger(L_, i); lua_setglobal(L_, waves[i]); }
    lua_pushinteger(L_, kWaveNoise); lua_setglobal(L_, "NOISE");
    lua_pushinteger(L_, kBgMapW); lua_setglobal(L_, "MAP_W");
    lua_pushinteger(L_, kBgMapH); lua_setglobal(L_, "MAP_H");
}

// ---- lifecycle -------------------------------------------------------------
Runtime::Runtime(FpgaDevice& dev, size_t heapCap) : dev_(dev) {
    heap_.cap = heapCap;
    L_ = lua_newstate(s8_alloc, &heap_, 0);   // 5.5: 3rd arg = seed (0 = deterministic)
    *static_cast<Runtime**>(lua_getextraspace(L_)) = this;

    // Safe embedded subset: base + table + string + math (no io/os/package).
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base}, {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
        {nullptr, nullptr},
    };
    for (const luaL_Reg* lib = libs; lib->func; ++lib) {
        luaL_requiref(L_, lib->name, lib->func, 1);
        lua_pop(L_, 1);
    }
    registerApi();
}

Runtime::~Runtime() { if (L_) lua_close(L_); }

void Runtime::setError(const char* ctx) {
    const char* msg = lua_tostring(L_, -1);
    std::snprintf(err_, sizeof err_, "%s: %s", ctx, msg ? msg : "(no message)");
    lua_pop(L_, 1);
}

bool Runtime::loadCartSource(const char* path) {
    if (luaL_loadfilex(L_, path, nullptr) != LUA_OK) { setError("load"); return false; }
    if (lua_pcall(L_, 0, 0, 0) != LUA_OK)            { setError("run");  return false; }
    return true;
}

bool Runtime::loadCart(const char* path) {
    // read the whole file
    FILE* fp = std::fopen(path, "rb");
    if (!fp) { std::snprintf(err_, sizeof err_, "cart: cannot open %s", path); return false; }
    std::fseek(fp, 0, SEEK_END); long n = std::ftell(fp); std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> buf(size_t(n < 0 ? 0 : n));
    if (!buf.empty() && std::fread(buf.data(), 1, buf.size(), fp) != buf.size()) {
        std::fclose(fp); std::snprintf(err_, sizeof err_, "cart: short read"); return false;
    }
    std::fclose(fp);

    CartView cart = parseCart(buf.data(), buf.size());
    if (!cart.valid) { std::snprintf(err_, sizeof err_, "cart: bad magic/version"); return false; }

    const uint8_t* p; uint32_t len;

    // META: title[32], author[32]
    if (cart.section(CartSection::Meta, p, len) && len >= 64) {
        std::memcpy(title_,  p,      32); title_[32] = 0;
        std::memcpy(author_, p + 32, 32); author_[32] = 0;
    }
    // PALETTE: repeated (index u8, r u8, g u8, b u8)
    if (cart.section(CartSection::Palette, p, len)) {
        for (uint32_t i = 0; i + 4 <= len; i += 4) {
            Color12 c{ uint8_t(p[i+1] & 15), uint8_t(p[i+2] & 15), uint8_t(p[i+3] & 15) };
            dev_.setPalette(p[i], std::span<const Color12>(&c, 1));
        }
    }
    // TILES: raw 4bpp tile-bank bytes -> SDRAM bank 0
    if (cart.section(CartSection::Tiles, p, len)) {
        dev_.loadBegin(0, len);
        dev_.loadData(std::span<const uint8_t>(p, len));
        dev_.loadEnd(0);
        dev_.bankActivate(0);
    }
    // MAP0 / MAP1: packed 16-bit cells (see cart.h packCell)
    auto loadMap = [&](CartSection sec, uint8_t layer) {
        const uint8_t* mp; uint32_t ml;
        if (!cart.section(sec, mp, ml)) return;
        std::vector<TileCell> cells(ml / 2);
        for (size_t i = 0; i < cells.size(); ++i) {
            uint16_t v = rd16(mp + i * 2);
            cells[i] = { uint16_t(v & 0x3FF), uint8_t((v >> 10) & 3),
                         bool((v >> 12) & 1), bool((v >> 13) & 1), bool((v >> 14) & 1) };
        }
        dev_.writeTilemap(layer, 0, cells);
    };
    loadMap(CartSection::Map0, 0);
    loadMap(CartSection::Map1, 1);

    // BYTECODE: undump and run the chunk
    if (!cart.section(CartSection::Bytecode, p, len)) {
        std::snprintf(err_, sizeof err_, "cart: no bytecode section"); return false;
    }
    if (luaL_loadbufferx(L_, reinterpret_cast<const char*>(p), len, "@cart", "b") != LUA_OK) {
        setError("cart bytecode"); return false;
    }
    if (lua_pcall(L_, 0, 0, 0) != LUA_OK) { setError("cart run"); return false; }
    return true;
}

void Runtime::callGlobal(const char* name) {
    lua_getglobal(L_, name);
    if (!lua_isfunction(L_, -1)) { lua_pop(L_, 1); return; }   // optional
    if (lua_pcall(L_, 0, 0, 0) != LUA_OK) setError(name);
}

void Runtime::callInit() { callGlobal("_init"); }

void Runtime::runFrame(const InputState& in) {
    input_ = in;
    spriteCount_ = 0;

    callGlobal("_update");
    callGlobal("_draw");

    // Flush the frame the way the MCU sends it to the FPGA.
    dev_.writeOam(std::span<const OamEntry>(oam_.data(), size_t(spriteCount_)));
    for (uint8_t l = 0; l < kNumBgLayers; ++l)
        dev_.setScroll(l, scrollX_[l], scrollY_[l]);
    dev_.swap();

    prevInput_ = input_;
}

// ---- binding-facing helpers ------------------------------------------------
const PadState& Runtime::pad(int player) const { return input_.pad[player]; }

bool Runtime::pressed(int button, int player) const {
    if (player < 0 || player >= kMaxPlayers ||
        button < 0 || button >= int(Button::Count)) return false;
    const Button b = Button(button);
    return input_.pad[player].down(b) && !prevInput_.pad[player].down(b);
}

void Runtime::pushSprite(const OamEntry& e) {
    if (spriteCount_ < kMaxSprites) oam_[spriteCount_++] = e;
}

void Runtime::setCamera(int16_t x, int16_t y) {
    scrollX_[curLayer_] = x;
    scrollY_[curLayer_] = y;
}

} // namespace s8
