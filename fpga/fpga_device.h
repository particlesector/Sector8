// fpga/fpga_device.h
//
// Sector8 — the MCU -> FPGA command boundary (the central contract, §5.3).
//
// FpgaDevice is a *semantic* interface: one method per Appendix A.4 opcode.
// s8_mcu builds render commands against this and nothing else.
//
//   v0.1 (now):   SoftFpga implements it with direct C++ work.
//   later:        VerilatedFpga implements the SAME interface by encoding each
//                 call into A.2 packets (CRC8 + framing) and clocking the
//                 Verilated SPI slave. s8_mcu recompiles unchanged.
//
// Discipline that keeps the seam real: a method must never do something a wire
// packet can't carry. If you're tempted, it belongs in s8_mcu, not here.
//
// Output (the rendered frame + audio) is a SEPARATE seam (VideoOutput, below),
// because on real silicon it comes off the HDMI signal, not the command bus.
//
// Constraints: C++20, no exceptions, no RTTI.

#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include "s8_types.h"   // protocol/s8_types.h

namespace s8 {

// ===========================================================================
// Command-in: 1:1 with Appendix A.4. All per-frame writes land in the BACK
// buffer and become visible only on swap() at the next vblank (§A.6).
// ===========================================================================
class FpgaDevice {
public:
    virtual ~FpgaDevice() = default;

    // -- Per-frame state -> back buffer --------------------------------------
    // WRITE_OAM: full sprite table, resent each frame (self-heals drops, §A.3).
    virtual void writeOam(std::span<const OamEntry> sprites) = 0;

    // SET_SCROLL: per-layer scroll, resent each frame.
    virtual void setScroll(uint8_t layer, int16_t x, int16_t y) = 0;

    // WRITE_TILEMAP: changed cells only (delta fast path). `addr` is the cell
    // offset into the layer's map. The rolling full-map resync (§A.3) is just
    // s8_mcu re-emitting spans over an N-frame cycle — no special call.
    virtual void writeTilemap(uint8_t layer, uint16_t addr,
                              std::span<const TileCell> cells) = 0;

    // SET_PALETTE: write `entries` starting at LUT index `first` (0..63).
    virtual void setPalette(uint8_t first, std::span<const Color12> entries) = 0;

    // SET_OVERLAY_MODE.
    virtual void setOverlayMode(OverlayMode mode) = 0;

    // Select which 64-color LUT sub-palette (0..3) the overlay's 4-bit indices
    // resolve through. Index 0 is always transparent; 1..15 map to sub*16 + i.
    virtual void setOverlayPalette(uint8_t sub) = 0;

    // -- Overlay draw -> back overlay (FPGA rasterizes; §3.6) -----------------
    // Coords are in the active overlay mode's space (320x180 or 160x90).
    virtual void ovlPixel    (int x, int y, uint8_t color) = 0;
    virtual void ovlLine     (int x0, int y0, int x1, int y1, uint8_t color) = 0;
    virtual void ovlRect     (int x, int y, int w, int h, uint8_t color) = 0;
    virtual void ovlFillRect (int x, int y, int w, int h, uint8_t color) = 0;
    virtual void ovlCircle   (int x, int y, int r, uint8_t color) = 0;
    virtual void ovlFillCircle(int x, int y, int r, uint8_t color) = 0;
    virtual void ovlText     (int x, int y, uint8_t color, std::string_view s) = 0;
    virtual void ovlClip     (int x, int y, int w, int h) = 0;

    // OVL_BLIT: bulk-push a packed 4bpp region (full-screen sw render path).
    // `packed` is row-major, 2 px/byte, length == ceil(w*h/2).
    virtual void ovlBlit(int x, int y, int w, int h,
                         std::span<const uint8_t> packed) = 0;

    // -- Audio / APU (§3.7): 4-channel wavetable DDS ------------------------
    // Play/retrigger a channel: `wave` 0..7 selects a wavetable slot (0..3 are
    // the preloaded classics), `wave` == 8 selects LFSR noise. volume 0..15.
    virtual void audioSetChannel(uint8_t ch, uint16_t freqHz,
                                 uint8_t wave, uint8_t volume) = 0;
    // Gate off: begin the release ramp to silence.
    virtual void audioNoteOff(uint8_t ch) = 0;
    // Load a custom 32-sample wavetable (int8) into a slot.
    virtual void audioLoadWavetable(uint8_t slot, std::span<const int8_t> samples) = 0;

    // -- Sequencer (§3.7): instruments, SFX patterns ------------------------
    // Instrument = a wavetable slot (or noise) + a 4-byte ADSR envelope.
    virtual void audioSetInstrument(uint8_t id, uint8_t wave,
                                    uint8_t a, uint8_t d, uint8_t s, uint8_t r) = 0;
    // Define an SFX: `speed` ticks per step, then up to 32 steps.
    virtual void audioSetSfx(uint8_t id, uint8_t speed, std::span<const SfxStep> steps) = 0;
    // Play SFX `id`; channel < 0 = auto-pick (steal the quietest if all busy).
    virtual void audioPlaySfx(uint8_t id, int channel) = 0;

    // -- Music sequencer (§3.7): chains of SFX across the 4 channels ---------
    // Define a music pattern: an SFX id per channel (0xFF = none) + flags
    // (bit0 loop-start, bit1 loop-end, bit2 stop).
    virtual void audioSetMusic(uint8_t id, std::span<const uint8_t> channelSfx,
                               uint8_t flags) = 0;
    // Start playing at pattern `track` (auto-advances/loops); track < 0 = stop.
    virtual void music(int track, int command) = 0;

    // -- Asset loads (bursty; paced by READY on real transport, §A.4) --------
    virtual void loadBegin(uint32_t destAddr, uint32_t length) = 0;
    virtual void loadData (std::span<const uint8_t> chunk) = 0;
    virtual void loadEnd  (uint32_t blobCrc) = 0;   // verified after DMA
    virtual void bankActivate(uint8_t bankId) = 0;  // scene-boundary swap

    // -- Control -------------------------------------------------------------
    virtual void swap() = 0;                         // commit back -> active @ vblank
    virtual ProtocolVersion hello(ProtocolVersion mcu) = 0;  // negotiate
    virtual Status status() = 0;
    virtual void ping()  = 0;
    virtual void reset() = 0;
};

// ===========================================================================
// Output-pull seam (§ seam #2). The host reads the scanned-out frame + audio.
// Concrete devices implement both interfaces; under Verilator this is driven
// off the simulated video/audio signals instead of a memcpy.
// ===========================================================================
class VideoOutput {
public:
    virtual ~VideoOutput() = default;

    // Most-recently scanned-out frame (the committed/active buffer).
    virtual VideoFrame frame() const = 0;

    // Pull up to `maxFrames` interleaved stereo S16 samples; returns frames
    // written. Lets the host's audio callback drain the APU at its own rate.
    virtual int pullAudio(int16_t* dst, int maxFrames) = 0;
};

} // namespace s8
