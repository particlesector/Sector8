# CMakeLists.txt — Sector8 PC simulator (M1 + Lua runtime)
#
# Layout:
#   .              CMakeLists.txt, vcpkg.json, s8_types.h, luaconf_s8.h,
#                  smoke_m1.cpp, mcu_test.cpp
#   app/           main.cpp
#   fpga/          fpga_device.h, soft_fpga.{h,cpp}
#   host/          host.{h}, host_sdl3.{h,cpp}
#   mcu/           input.h, runtime.{h,cpp}
#   carts/         hello.lua
#   third_party/lua/   <- vendored Lua 5.5.0 source (git submodule)
#
# Build:  cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
#         cmake --build build --config Release
#         build\Release\s8sim.exe carts\hello.lua

cmake_minimum_required(VERSION 3.16)
project(sector8 CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SDL3 REQUIRED CONFIG)

set(S8_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fpga
    ${CMAKE_CURRENT_SOURCE_DIR}/host
    ${CMAKE_CURRENT_SOURCE_DIR}/mcu)

# --- Force-include the pinned Lua config into anything that touches lua.h ----
# One source of truth for LUA_32BITS across sim/firmware/luac -> portable bytecode.
set(S8_LUACONF ${CMAKE_CURRENT_SOURCE_DIR}/luaconf_s8.h)
function(s8_pin_luaconf tgt)
  if(MSVC)
    target_compile_options(${tgt} PRIVATE "/FI${S8_LUACONF}")
  else()
    target_compile_options(${tgt} PRIVATE -include "${S8_LUACONF}")
  endif()
endfunction()

# --- vendored Lua 5.5.0 -> static lib ---------------------------------------
set(LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lua)
if(NOT EXISTS ${LUA_DIR}/lua.h)
  message(FATAL_ERROR "Lua source not found in third_party/lua. "
                      "Run: git submodule update --init  (see README).")
endif()
file(GLOB LUA_SRC ${LUA_DIR}/*.c)
# Exclude the standalone mains, the amalgamation, and the internal test hook.
list(FILTER LUA_SRC EXCLUDE REGEX "/(lua|luac|onelua|ltests)\\.c$")
add_library(lua_s8 STATIC ${LUA_SRC})
target_include_directories(lua_s8 PUBLIC ${LUA_DIR})
s8_pin_luaconf(lua_s8)

# --- FPGA model (host-only, SDL-free, Lua-free) -----------------------------
add_library(s8_fpga STATIC fpga/soft_fpga.cpp)
target_include_directories(s8_fpga PUBLIC ${S8_INCLUDE_DIRS})

# --- MCU runtime (embeds Lua) -----------------------------------------------
add_library(s8_mcu STATIC mcu/runtime.cpp)
target_include_directories(s8_mcu PUBLIC ${S8_INCLUDE_DIRS})
target_link_libraries(s8_mcu PUBLIC s8_fpga lua_s8)
s8_pin_luaconf(s8_mcu)   # runtime.cpp includes lua.h -> needs LUA_32BITS too

# --- the simulator ----------------------------------------------------------
add_executable(s8sim app/main.cpp host/host_sdl3.cpp)
target_include_directories(s8sim PRIVATE ${S8_INCLUDE_DIRS})
target_link_libraries(s8sim PRIVATE s8_mcu SDL3::SDL3)

# --- headless tests (no SDL) ------------------------------------------------
add_executable(s8_smoke smoke_m1.cpp)
target_include_directories(s8_smoke PRIVATE ${S8_INCLUDE_DIRS})
target_link_libraries(s8_smoke PRIVATE s8_fpga)

add_executable(s8_mcu_test mcu_test.cpp)
target_include_directories(s8_mcu_test PRIVATE ${S8_INCLUDE_DIRS})
target_link_libraries(s8_mcu_test PRIVATE s8_mcu)

# Headless overlay test (no SDL).  NOTE: place font8x8_s8.h in fpga/.
add_executable(s8_overlay_test overlay_test.cpp)
target_include_directories(s8_overlay_test PRIVATE ${S8_INCLUDE_DIRS})
target_link_libraries(s8_overlay_test PRIVATE s8_fpga)

# Headless BG1 + priority test (no SDL).
add_executable(s8_bg1_test bg1_test.cpp)
target_include_directories(s8_bg1_test PRIVATE ${S8_INCLUDE_DIRS})
target_link_libraries(s8_bg1_test PRIVATE s8_fpga)
