// include/luaconf_s8.h
//
// Sector8's pinned Lua configuration. Force-include this (gcc/clang: -include,
// MSVC: /FI) into EVERY translation unit that touches Lua — the vendored Lua
// sources, the runtime, the firmware, and (later) luac — so lua_Integer /
// lua_Number widths match everywhere and precompiled bytecode is portable.
//
// It must be seen before <luaconf.h>, which reads LUA_32BITS.

#pragma once

// 32-bit lua_Integer + single-precision lua_Number: matches the RP2350's M33
// single-precision FPU and keeps the bytecode number format identical on host
// and device.
#define LUA_32BITS 1
