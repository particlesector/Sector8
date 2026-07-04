// fpga/soft_fpga.h
//
// Sector8 — software FPGA model (host-only; the real FPGA runs Verilog, not this).
// Because it never targets ARM, it may use the full host STL freely; only the
// contract headers (s8_types.h, fpga_device.h) must stay ARM-safe.
//
// Milestone M1 scope: palette LUT, SDRAM + tile bank, BG0, sprites, scanline
// compositor, double-buffer/swap. BG1 / overlay / APU are no-op stubs tagged
// with the milestone that fills them in.

#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include "fpga_device.h"   // fpga/fpga_device.h

namespace s8 {

class SoftFpga final : public FpgaDevice, public VideoOutput {
public:
    SoftFpga();

    // -- FpgaDevice: per-frame state ----------------------------------------
    void writeOam(std::span<const OamEntry> sprites) override;
    void setScroll(uint8_t layer, int16_t x, int16_t y) override;
    void writeTilemap(uint8_t layer, uint16_t addr,
                      std::span<const TileCell> cells) override;
    void setPalette(uint8_t first, std::span<const Color12> entries) override;
    void setOverlayMode(OverlayMode mode) override;
    void setOverlayPalette(uint8_t sub) override;

    // -- Overlay (M2): FPGA-rasterized primitives -----------------------------
    void ovlPixel(int x, int y, uint8_t color) override;
    void ovlLine(int x0, int y0, int x1, int y1, uint8_t color) override;
    void ovlRect(int x, int y, int w, int h, uint8_t color) override;
    void ovlFillRect(int x, int y, int w, int h, uint8_t color) override;
    void ovlCircle(int x, int y, int r, uint8_t color) override;
    void ovlFillCircle(int x, int y, int r, uint8_t color) override;
    void ovlText(int x, int y, uint8_t color, std::string_view s) override;
    void ovlClip(int x, int y, int w, int h) override;
    void ovlBlit(int x, int y, int w, int h, std::span<const uint8_t> packed) override;

    // -- Audio / APU --------------------------------------------------------
    void audioSetChannel(uint8_t ch, uint16_t freqHz, uint8_t wave, uint8_t volume) override;
    void audioNoteOff(uint8_t ch) override;
    void audioLoadWavetable(uint8_t slot, std::span<const int8_t> samples) override;
    void audioSetInstrument(uint8_t id, uint8_t wave, uint8_t a, uint8_t d, uint8_t s, uint8_t r) override;
    void audioSetSfx(uint8_t id, uint8_t speed, std::span<const SfxStep> steps) override;
    void audioPlaySfx(uint8_t id, int channel) override;
    void audioSetMusic(uint8_t id, std::span<const uint8_t> channelSfx, uint8_t flags) override;
    void music(int track, int command) override;

    // -- Asset loads --------------------------------------------------------
    void loadBegin(uint32_t destAddr, uint32_t length) override;
    void loadData(std::span<const uint8_t> chunk) override;
    void loadEnd(uint32_t blobCrc) override;
    void bankActivate(uint8_t bankId) override;

    // -- Control ------------------------------------------------------------
    void swap() override;
    ProtocolVersion hello(ProtocolVersion mcu) override;
    Status status() override;
    void ping() override {}
    void reset() override;

    // -- VideoOutput --------------------------------------------------------
    VideoFrame frame() const override;
    int pullAudio(int16_t* dst, int maxFrames) override;   // synth -> stereo S16

private:
    static constexpr int kMapW        = 64;                 // cells per map row
    static constexpr int kMapH        = 64;
    static constexpr int kMapPixW     = kMapW * kTile;      // 512
    static constexpr int kMapPixH     = kMapH * kTile;
    static constexpr int kBytesPerTile= (kTile * kTile) / 2;// 32 (4bpp)
    static constexpr int kSdramBytes  = 8 * 1024 * 1024;

    // Double-buffered display state (§A.6). Commit is a value-copy on swap(),
    // which gives correct tilemap copy-on-swap for free: after active_=back_,
    // back_ still equals what was just shown, so next frame's deltas land on
    // current state. OAM/scroll/palette are resent in full each frame anyway.
    struct RegSet {
        std::array<OamEntry, kMaxSprites>                 oam{};
        std::array<int16_t, kNumBgLayers>                 scrollX{};
        std::array<int16_t, kNumBgLayers>                 scrollY{};
        std::array<std::array<TileCell, kMapW * kMapH>, kNumBgLayers> map{};
        std::array<Color12, kPaletteEntries>              palette{};
        OverlayMode                                       overlayMode =
                                                          OverlayMode::Native320x180;
        uint8_t                                           overlayPalette = 0;  // sub-palette 0..3
    };
    RegSet back_{};
    RegSet active_{};

    std::vector<uint8_t> sdram_;        // asset memory (tile/sound banks)
    uint32_t tileBankBase_ = 0;         // active tile bank base (BANK_ACTIVATE)
    uint32_t loadCursor_   = 0;         // current asset-load write address

    std::array<uint32_t, kScreenW * kScreenH> fb_{};   // 0xRRGGBBAA output
    std::array<uint32_t, kPaletteEntries>     rgb_{};   // active palette, expanded
    Status statusRegs_{};

    // Overlay: 4bpp index buffers (one byte/pixel in the model), double-buffered
    // separately from RegSet. ovlBack_ is drawn into; committed + cleared on swap.
    std::array<uint8_t, kScreenW * kScreenH> ovlActive_{};
    std::array<uint8_t, kScreenW * kScreenH> ovlBack_{};
    int clipX0_ = 0, clipY0_ = 0, clipX1_ = kScreenW, clipY1_ = kScreenH;  // logical

    // --- APU: 4-channel wavetable DDS (fixed-point) ---
    struct ApuChannel {
        uint32_t phase    = 0;      // 32-bit phase accumulator
        uint32_t phaseInc = 0;      // per-sample increment = freq * 2^32 / rate
        uint8_t  waveSlot = 0;      // wavetable slot 0..7
        bool     noise    = false;  // LFSR mode
        uint8_t  volume   = 0;      // 0..15
        uint16_t lfsr     = 0x7FFF; // noise shift register (15-bit)
        uint8_t  lastIdx  = 0;      // for clocking noise at the wavetable rate

        // ADSR envelope (fixed-point, level 0..kEnvUnity)
        enum EnvPhase : uint8_t { Idle, Attack, Decay, Sustain, Release };
        EnvPhase envPhase = Idle;
        int32_t  envLevel = 0;
        int32_t  attackInc = 0, decayInc = 0, releaseInc = 0, sustainLevel = 0;

        // per-channel SFX player
        bool     sfxActive = false;
        bool     musicOwned = false;   // launched by the music sequencer
        uint8_t  sfxId = 0, sfxStep = 0;
        uint32_t sfxCtr = 0, sfxStepSamples = 0;
    };
    static constexpr int32_t kEnvUnity = 1 << 20;   // full envelope level (power of 2 -> shift)
    ApuChannel apu_[kAudioChannels];
    int8_t wavetables_[kWavetableSlots][kWavetableLen] = {};

    struct Instrument { uint8_t wave = 0, a = 0, d = 0, s = 255, r = 0; };
    struct Sfx { uint8_t speed = 8; SfxStep steps[kSfxSteps] = {}; };
    Instrument instruments_[kInstrumentSlots] = {};
    Sfx        sfx_[kSfxSlots] = {};
    uint16_t   noteFreq_[128] = {};      // semitone -> Hz (equal temperament)

    // Music sequencer: patterns chain SFX across the 4 channels and loop.
    struct MusicPattern {
        uint8_t sfx[kAudioChannels] = {0xFF, 0xFF, 0xFF, 0xFF};  // 0xFF = none
        bool loopStart = false, loopEnd = false, stop = false;
    };
    MusicPattern music_[kMusicPatterns] = {};
    bool     musicActive = false;
    int      musicCur = 0, musicLoopStart = 0;
    uint32_t musicCtr = 0, musicPatternSamples = 0;

    // pipeline
    void rasterize();                   // active_ -> fb_
    void compositeScanline(int y);
    void compositeBgLayer(int layer, int y, uint32_t* row, bool* prio);
    uint8_t tilePixel(uint16_t tile, int px, int py) const;  // 4-bit index
    void rebuildPaletteCache();
    static uint32_t expand12(Color12 c);

    // overlay helpers
    int  ovlW() const { return back_.overlayMode == OverlayMode::Pure160x90 ? 160 : kScreenW; }
    int  ovlH() const { return back_.overlayMode == OverlayMode::Pure160x90 ? 90  : kScreenH; }
    void ovlPlot(int lx, int ly, uint8_t c);   // logical coords, clip + mode-scale
    void resetClip();
    void initWavetables();                      // preload classic waveforms + note table

    // APU internals
    void triggerNote(ApuChannel& c, uint16_t freq, uint8_t wave, uint8_t vol,
                     int32_t attackInc, int32_t decayInc, int32_t sustainLevel, int32_t releaseInc);
    void advanceEnv(ApuChannel& c);
    void applySfxStep(ApuChannel& c);
    void launchSfx(uint8_t id, int ch, bool musicOwned);
    void startMusicPattern();
    int  pickChannel() const;                   // idle, else quietest (steal-with-priority)
    static int32_t envIncForTime(uint8_t param, int32_t span);
};

} // namespace s8
