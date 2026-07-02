// mcu/runtime.cpp  — Sector8 MCU runtime + Lua API bindings.

#include "runtime.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

void Runtime::registerApi() {
    const luaL_Reg api[] = {
        {"sprite", l_sprite}, {"spr", l_sprite},
        {"camera", l_camera},
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
