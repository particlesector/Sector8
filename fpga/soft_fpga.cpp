// fpga/soft_fpga.cpp  — Sector8 software FPGA model, milestone M1.

#include "soft_fpga.h"
#include "font8x8_s8.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace s8 {

namespace {
// Positive modulo for scroll wrap.
inline int wrap(int v, int m) { int r = v % m; return r < 0 ? r + m : r; }

// 4bpp packing convention (shared with OVL_BLIT): two pixels per byte, the
// left/even pixel in the HIGH nibble.
inline uint8_t nibble(uint8_t byte, int evenPixel) {
    return evenPixel ? uint8_t(byte >> 4) : uint8_t(byte & 0x0F);
}
} // namespace

SoftFpga::SoftFpga() : sdram_(kSdramBytes, 0) { reset(); }

// ---- per-frame state writes (into back_) ----------------------------------

void SoftFpga::writeOam(std::span<const OamEntry> sprites) {
    // WRITE_OAM replaces the whole table; unspecified entries go disabled.
    back_.oam.fill(OamEntry{});
    const size_t n = std::min<size_t>(sprites.size(), kMaxSprites);
    std::copy_n(sprites.begin(), n, back_.oam.begin());
}

void SoftFpga::setScroll(uint8_t layer, int16_t x, int16_t y) {
    if (layer >= kNumBgLayers) return;
    back_.scrollX[layer] = x;
    back_.scrollY[layer] = y;
}

void SoftFpga::writeTilemap(uint8_t layer, uint16_t addr,
                            std::span<const TileCell> cells) {
    if (layer >= kNumBgLayers) return;
    auto& map = back_.map[layer];
    for (size_t i = 0; i < cells.size(); ++i) {
        const size_t cell = size_t(addr) + i;
        if (cell >= map.size()) break;
        map[cell] = cells[i];
    }
}

void SoftFpga::setPalette(uint8_t first, std::span<const Color12> entries) {
    for (size_t i = 0; i < entries.size(); ++i) {
        const size_t idx = size_t(first) + i;
        if (idx >= kPaletteEntries) break;
        back_.palette[idx] = entries[i];
    }
}

void SoftFpga::setOverlayMode(OverlayMode mode) { back_.overlayMode = mode; }

void SoftFpga::setOverlayPalette(uint8_t sub) {
    back_.overlayPalette = sub < kSubPalettes ? sub : 0;
}

// ---- overlay rasterizer ----------------------------------------------------
// One logical pixel -> a 1x1 (Mode 0) or 2x2 (Mode 1) block in the physical
// 320x180 index buffer. Clip is tested in logical space.
void SoftFpga::ovlPlot(int lx, int ly, uint8_t c) {
    if (lx < clipX0_ || lx >= clipX1_ || ly < clipY0_ || ly >= clipY1_) return;
    const int scale = (back_.overlayMode == OverlayMode::Pure160x90) ? 2 : 1;
    const int px0 = lx * scale, py0 = ly * scale;
    for (int dy = 0; dy < scale; ++dy)
        for (int dx = 0; dx < scale; ++dx) {
            const int px = px0 + dx, py = py0 + dy;
            if (px >= 0 && px < kScreenW && py >= 0 && py < kScreenH)
                ovlBack_[py * kScreenW + px] = uint8_t(c & 0x0F);
        }
}

void SoftFpga::resetClip() {
    clipX0_ = 0; clipY0_ = 0; clipX1_ = ovlW(); clipY1_ = ovlH();
}

void SoftFpga::ovlPixel(int x, int y, uint8_t c) { ovlPlot(x, y, c); }

void SoftFpga::ovlLine(int x0, int y0, int x1, int y1, uint8_t c) {
    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        ovlPlot(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void SoftFpga::ovlFillRect(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) ovlPlot(x + i, y + j, c);
}

void SoftFpga::ovlRect(int x, int y, int w, int h, uint8_t c) {
    if (w <= 0 || h <= 0) return;
    for (int i = 0; i < w; ++i) { ovlPlot(x + i, y, c); ovlPlot(x + i, y + h - 1, c); }
    for (int j = 0; j < h; ++j) { ovlPlot(x, y + j, c); ovlPlot(x + w - 1, y + j, c); }
}

void SoftFpga::ovlFillCircle(int x, int y, int r, uint8_t c) {
    if (r < 0) return;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) ovlPlot(x + dx, y + dy, c);
}

void SoftFpga::ovlCircle(int x, int y, int r, uint8_t c) {
    int px = r, py = 0, err = 1 - r;
    while (px >= py) {
        ovlPlot(x + px, y + py, c); ovlPlot(x - px, y + py, c);
        ovlPlot(x + px, y - py, c); ovlPlot(x - px, y - py, c);
        ovlPlot(x + py, y + px, c); ovlPlot(x - py, y + px, c);
        ovlPlot(x + py, y - px, c); ovlPlot(x - py, y - px, c);
        ++py;
        if (err < 0) err += 2 * py + 1;
        else { --px; err += 2 * (py - px) + 1; }
    }
}

void SoftFpga::ovlText(int x, int y, uint8_t c, std::string_view s) {
    int cx = x;
    for (char raw : s) {
        unsigned ch = static_cast<unsigned char>(raw);
        if (ch == '\n') { cx = x; y += 8; continue; }
        if (ch < kFontFirst || ch >= unsigned(kFontFirst + kFontCount)) ch = '?';
        const uint8_t* g = kFont8x8[ch - kFontFirst];
        for (int row = 0; row < 8; ++row)
            for (int col = 0; col < 8; ++col)
                if ((g[row] >> col) & 1) ovlPlot(cx + col, y + row, c);  // bit0 = left
        cx += 8;
    }
}

void SoftFpga::ovlClip(int x, int y, int w, int h) {
    clipX0_ = std::max(0, x);
    clipY0_ = std::max(0, y);
    clipX1_ = std::min(ovlW(), x + w);
    clipY1_ = std::min(ovlH(), y + h);
}

void SoftFpga::ovlBlit(int x, int y, int w, int h, std::span<const uint8_t> packed) {
    // Row-major, 2 px/byte, left pixel in the high nibble (tile convention).
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const size_t bit = size_t(j) * w + i;
            const size_t byte = bit >> 1;
            if (byte >= packed.size()) return;
            const uint8_t v = (i & 1) ? (packed[byte] & 0x0F) : (packed[byte] >> 4);
            ovlPlot(x + i, y + j, v);
        }
}

// ---- asset loads ----------------------------------------------------------

void SoftFpga::loadBegin(uint32_t destAddr, uint32_t /*length*/) {
    loadCursor_ = destAddr;
}

void SoftFpga::loadData(std::span<const uint8_t> chunk) {
    if (loadCursor_ + chunk.size() > sdram_.size()) return;  // guard
    std::memcpy(sdram_.data() + loadCursor_, chunk.data(), chunk.size());
    loadCursor_ += uint32_t(chunk.size());
}

void SoftFpga::loadEnd(uint32_t /*blobCrc*/) {
    // M1: CRC verification is a no-op; wire it in with the protocol layer.
}

void SoftFpga::bankActivate(uint8_t bankId) {
    // Simple bank model: fixed-stride windows of the active-bank size.
    tileBankBase_ = uint32_t(bankId) * uint32_t(kActiveTiles) * kBytesPerTile;
}

// ---- control --------------------------------------------------------------

void SoftFpga::swap() {
    ovlActive_ = ovlBack_;   // commit overlay to display
    active_ = back_;         // commit registers (+ implicit tilemap copy-on-swap)
    ovlBack_.fill(0);        // auto-clear overlay to transparent each vblank (§3.6)
    resetClip();             // clip bounds reset each frame
    rasterize();             // produce the frame the host will pull via frame()
}

ProtocolVersion SoftFpga::hello(ProtocolVersion /*mcu*/) {
    // Single-implementation v0.1: echo our own version (negotiation is a stub).
    return statusRegs_.version;
}

Status SoftFpga::status() { return statusRegs_; }

void SoftFpga::reset() {
    back_ = RegSet{};
    active_ = RegSet{};
    tileBankBase_ = 0;
    loadCursor_ = 0;
    fb_.fill(0x000000FFu);
    rgb_.fill(0x000000FFu);
    ovlActive_.fill(0);
    ovlBack_.fill(0);
    resetClip();
    statusRegs_ = Status{};
}

// ---- output ---------------------------------------------------------------

VideoFrame SoftFpga::frame() const {
    return VideoFrame{ fb_.data(), kScreenW, kScreenH };
}

// ---- rasterizer -----------------------------------------------------------

uint32_t SoftFpga::expand12(Color12 c) {
    const uint32_t r = (c.r << 4) | c.r;   // 0..15 -> 0..255
    const uint32_t g = (c.g << 4) | c.g;
    const uint32_t b = (c.b << 4) | c.b;
    return (r << 24) | (g << 16) | (b << 8) | 0xFFu;  // 0xRRGGBBAA
}

void SoftFpga::rebuildPaletteCache() {
    for (int i = 0; i < kPaletteEntries; ++i)
        rgb_[i] = expand12(active_.palette[i]);
}

uint8_t SoftFpga::tilePixel(uint16_t tile, int px, int py) const {
    const uint32_t base = tileBankBase_ + uint32_t(tile) * kBytesPerTile;
    const uint32_t off  = base + uint32_t(py) * (kTile / 2) + uint32_t(px >> 1);
    if (off >= sdram_.size()) return 0;
    return nibble(sdram_[off], (px & 1) == 0);
}

void SoftFpga::rasterize() {
    rebuildPaletteCache();
    for (int y = 0; y < kScreenH; ++y) compositeScanline(y);
}

void SoftFpga::compositeBgLayer(int layer, int y, uint32_t* row, bool* prio) {
    const auto& map = active_.map[layer];
    const int sx = active_.scrollX[layer];
    const int sy = active_.scrollY[layer];
    const int wy = wrap(y + sy, kMapPixH);
    const int trow = wy / kTile;
    const int tpy  = wy % kTile;
    for (int x = 0; x < kScreenW; ++x) {
        const int wx = wrap(x + sx, kMapPixW);
        const TileCell& c = map[trow * kMapW + (wx / kTile)];
        int px = wx % kTile, py = tpy;
        if (c.flip_h) px = kTile - 1 - px;
        if (c.flip_v) py = kTile - 1 - py;
        const uint8_t idx = tilePixel(c.tile, px, py);
        if (idx != 0) {                     // index 0 == transparent
            row[x]  = rgb_[c.palette * kPaletteSize + idx];
            prio[x] = c.priority;           // frontmost opaque BG cell's priority
        }
    }
}

void SoftFpga::compositeScanline(int y) {
    uint32_t* row = &fb_[y * kScreenW];

    // Backdrop = sub-palette 0, index 0. prio[x] tracks whether the visible BG
    // pixel wants to sit ABOVE sprites (the per-cell priority bit).
    const uint32_t backdrop = rgb_[0];
    bool prio[kScreenW];
    for (int x = 0; x < kScreenW; ++x) { row[x] = backdrop; prio[x] = false; }

    // Back to front: BG0, then BG1 (overrides BG0 and its priority where opaque).
    compositeBgLayer(0, y, row, prio);
    compositeBgLayer(1, y, row, prio);

    // --- Sprites: 16/line cap, lower OAM index wins (drawn last = on top) ---
    int chosen[kMaxSpritesPerLine];
    int count = 0;
    for (int i = 0; i < kMaxSprites && count < kMaxSpritesPerLine; ++i) {
        const OamEntry& s = active_.oam[i];
        if (!s.enabled) continue;
        if (y < s.y || y >= s.y + kTile) continue;   // not on this line
        chosen[count++] = i;
    }
    for (int k = count - 1; k >= 0; --k) {           // reverse: index 0 on top
        const OamEntry& s = active_.oam[chosen[k]];
        const int py0 = y - s.y;
        for (int dx = 0; dx < kTile; ++dx) {
            const int sxp = s.x + dx;
            if (sxp < 0 || sxp >= kScreenW) continue;
            int px = dx, py = py0;
            if (s.flip_h) px = kTile - 1 - px;
            if (s.flip_v) py = kTile - 1 - py;
            const uint8_t idx = tilePixel(s.tile, px, py);
            if (idx == 0) continue;                  // color key 0 == transparent
            if (prio[sxp]) continue;                 // priority BG cell stays above sprites
            row[sxp] = rgb_[s.palette * kPaletteSize + idx];
        }
    }
    // --- Overlay: front-most layer; index 0 = transparent (§3.6) ---
    const uint8_t* orow = &ovlActive_[y * kScreenW];
    const int obase = active_.overlayPalette * kPaletteSize;
    for (int x = 0; x < kScreenW; ++x) {
        const uint8_t oi = orow[x];
        if (oi != 0) row[x] = rgb_[obase + oi];
    }
}

} // namespace s8
