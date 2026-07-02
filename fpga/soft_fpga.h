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

    // -- Audio (M3): stubs --------------------------------------------------
    void sound(int, int) override {}
    void music(int, int) override {}

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
    int pullAudio(int16_t*, int) override { return 0; }  // M3: silence

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
};

} // namespace s8
