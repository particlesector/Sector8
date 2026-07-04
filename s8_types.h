// protocol/s8_types.h
//
// Sector8 — shared value types for the logical machine.
//
// Used by three consumers: the MCU runtime (s8_mcu), the FPGA model (s8_fpga),
// and — later — the wire packet encoder behind the Verilator adapter.
//
// IMPORTANT: these are *in-memory ergonomic* representations, NOT the on-wire
// byte layout. The packet encoder packs fields explicitly (bit widths noted in
// comments); never assume sizeof(struct) == serialized size or rely on bitfield
// ordering. Keeping these layout-agnostic is what makes the same header safe on
// host x86 and ARM, and what keeps "direct calls now, packets later" honest.
//
// Constraints: C++20, no exceptions, no RTTI (must compile -fno-exceptions
// -fno-rtti for the ARM firmware build).

#pragma once
#include <cstdint>

namespace s8 {

// ---- Logical-machine constants (the contract; §3) -------------------------
inline constexpr int kScreenW          = 320;
inline constexpr int kScreenH          = 180;
inline constexpr int kTile             = 8;     // 8x8 tiles & sprites
inline constexpr int kMaxSprites       = 64;    // OAM depth (§3.5)
inline constexpr int kMaxSpritesPerLine= 16;    // per-scanline cap (§3.5)
inline constexpr int kActiveTiles      = 512;   // per active bank (§3.4)
inline constexpr int kTileIndexMax     = 1024;  // 10-bit field, 1024-ready
inline constexpr int kSubPalettes      = 4;     // (§3.1)
inline constexpr int kPaletteSize      = 16;    // colors per sub-palette
inline constexpr int kPaletteEntries   = kSubPalettes * kPaletteSize; // 64 LUT
inline constexpr int kNumBgLayers      = 2;     // BG0, BG1 — committed baseline
inline constexpr int kBgMapW           = 64;    // tilemap cells per row (per layer)
inline constexpr int kBgMapH           = 64;
inline constexpr int kAudioChannels    = 4;     // (§3.7)
inline constexpr int kWavetableSlots   = 8;     // 0..3 preloaded, 4..7 custom
inline constexpr int kWavetableLen     = 32;    // samples per wavetable (§3.7)
inline constexpr int kWaveNoise        = 8;     // sentinel: LFSR noise, not a slot
inline constexpr int kAudioSampleRate  = 48000; // matches the HDMI/SDL stream
inline constexpr int kInstrumentSlots  = 32;    // instrument definitions
inline constexpr int kSfxSlots         = 64;    // sound-effect patterns
inline constexpr int kMusicPatterns    = 64;    // song patterns (chains of SFX)
inline constexpr int kSfxSteps         = 32;    // steps per SFX pattern (§3.7)

// One step of an SFX pattern. vol == 0 means "rest" (release the channel).
struct SfxStep {
    uint8_t note = 0;   // semitone number (60 = C4)
    uint8_t inst = 0;   // instrument id
    uint8_t vol  = 0;   // 0..15; 0 = rest
};

// ---- Color: 12-bit RGB, 4 bits/channel (§3.1) -----------------------------
struct Color12 {
    uint8_t r = 0;  // 0..15
    uint8_t g = 0;  // 0..15
    uint8_t b = 0;  // 0..15
};

// ---- Shared tile/cell attributes (§3.4): the 15-bit "attribute word" ------
// A tilemap cell is exactly this (position is implicit in the grid).
struct TileCell {
    uint16_t tile     = 0;      // 10-bit index into the active bank
    uint8_t  palette  = 0;      // sub-palette 0..3
    bool     flip_h   = false;
    bool     flip_v   = false;
    bool     priority = false;  // raises this cell above sprites where set
};

// ---- OAM entry (§3.5): attribute word + explicit position -----------------
// Signed x/y so sprites can hang partially off the left/top edge.
struct OamEntry {
    int16_t  x        = 0;
    int16_t  y        = 0;
    uint16_t tile     = 0;      // 10-bit index
    uint8_t  palette  = 0;      // sub-palette 0..3
    bool     flip_h   = false;
    bool     flip_v   = false;
    bool     priority = false;
    bool     enabled  = false;  // disabled entries are skipped by the compositor
};

// ---- Overlay readout mode (§3.6) ------------------------------------------
enum class OverlayMode : uint8_t {
    Native320x180 = 0,  // crisp 1:1 accents
    Pure160x90    = 1,  // hardware nearest-neighbor x2 to fill screen
};

// ---- Protocol handshake (HELLO/VERSION) -----------------------------------
struct ProtocolVersion {
    uint8_t major = 0;
    uint8_t minor = 1;
};

// ---- STATUS: error counters + readiness -----------------------------------
struct Status {
    uint32_t crc_errors      = 0;  // per-frame packet CRC mismatches (§A.3)
    uint32_t dropped_packets = 0;
    bool     ready           = true;
    ProtocolVersion version  {};
};

// ---- Video output (post-LUT RGB, what HDMI would carry; pre-scale) ---------
// The host owns the x4 -> 720p scale; the model emits native 320x180.
struct VideoFrame {
    const uint32_t* pixels = nullptr;  // 0xRRGGBBAA, A=0xFF; row-major, tight
    int             width  = kScreenW;
    int             height = kScreenH;
};

} // namespace s8
