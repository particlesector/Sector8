// fpga/soft_fpga.cpp  — Sector8 software FPGA model, milestone M1.

#include "soft_fpga.h"
#include "font8x8_s8.h"
#include <algorithm>
#include <cmath>
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

SoftFpga::SoftFpga() : sdram_(kSdramBytes, 0) { initWavetables(); reset(); }

// Preloaded classic waveforms (§3.7). One-time table fill; the per-sample synth
// path below is pure integer/fixed-point. The FPGA bakes the same tables in ROM.
void SoftFpga::initWavetables() {
    constexpr double kPi = 3.14159265358979323846;   // local: only the sine fill needs it
    const int N = kWavetableLen;
    for (int i = 0; i < N; ++i) {
        wavetables_[0][i] = int8_t(i < N / 2 ? 127 : -128);              // square (50%)
        int tri = (i < N / 2) ? (-128 + i * 512 / N)                    // triangle
                              : (127 - (i - N / 2) * 512 / N);
        wavetables_[1][i] = int8_t(std::clamp(tri, -128, 127));
        wavetables_[2][i] = int8_t(-128 + i * 255 / (N - 1));            // saw
        wavetables_[3][i] = int8_t(std::lround(127.0 * std::sin(2.0 * kPi * i / N))); // sine
    }
    // slots 4..7 default to silence (all zero) until a custom wavetable loads.

    // Semitone -> Hz (equal temperament, A4=440 at note 69). One-time; the RTL
    // bakes the same table so the sequencer's note->freq is a pure lookup.
    for (int n = 0; n < 128; ++n)
        noteFreq_[n] = uint16_t(std::lround(440.0 * std::pow(2.0, (n - 69) / 12.0)));
}

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
    for (auto& c : apu_) c = ApuChannel{};   // wavetables_ persist (set in ctor)
    musicActive = false; musicCur = 0; musicLoopStart = 0;
    musicCtr = 0; musicPatternSamples = 0;
    statusRegs_ = Status{};
}

// ---- output ---------------------------------------------------------------

VideoFrame SoftFpga::frame() const {
    return VideoFrame{ fb_.data(), kScreenW, kScreenH };
}

// ---- APU: envelope + note triggering --------------------------------------
// Map an ADSR time param (0..255) to a per-sample increment covering `span`.
int32_t SoftFpga::envIncForTime(uint8_t param, int32_t span) {
    const int samples = int(param) * kAudioSampleRate / 256;   // param 256 ~= 1s
    if (samples <= 0) return span;                              // instant
    return std::max(1, span / samples);
}

void SoftFpga::triggerNote(ApuChannel& c, uint16_t freq, uint8_t wave, uint8_t vol,
                           int32_t attackInc, int32_t decayInc,
                           int32_t sustainLevel, int32_t releaseInc) {
    c.phase        = 0;
    c.phaseInc     = uint32_t((uint64_t(freq) << 32) / kAudioSampleRate);
    c.noise        = (wave == kWaveNoise);
    c.waveSlot     = c.noise ? 0 : uint8_t(wave & (kWavetableSlots - 1));
    c.volume       = vol > 15 ? 15 : vol;
    c.attackInc    = attackInc;
    c.decayInc     = decayInc;
    c.sustainLevel = sustainLevel;
    c.releaseInc   = releaseInc;
    c.envLevel     = 0;
    c.envPhase     = ApuChannel::Attack;
}

void SoftFpga::advanceEnv(ApuChannel& c) {
    switch (c.envPhase) {
        case ApuChannel::Attack:
            c.envLevel += c.attackInc;
            if (c.envLevel >= kEnvUnity) { c.envLevel = kEnvUnity; c.envPhase = ApuChannel::Decay; }
            break;
        case ApuChannel::Decay:
            c.envLevel -= c.decayInc;
            if (c.envLevel <= c.sustainLevel) { c.envLevel = c.sustainLevel; c.envPhase = ApuChannel::Sustain; }
            break;
        case ApuChannel::Sustain: break;
        case ApuChannel::Release:
            c.envLevel -= c.releaseInc;
            if (c.envLevel <= 0) { c.envLevel = 0; c.envPhase = ApuChannel::Idle; }
            break;
        case ApuChannel::Idle: break;
    }
}

// Low-level trigger (Part 1 `sound()`): instant on, full sustain, short release.
void SoftFpga::audioSetChannel(uint8_t ch, uint16_t freqHz, uint8_t wave, uint8_t volume) {
    if (ch >= kAudioChannels) return;
    ApuChannel& c = apu_[ch];
    c.sfxActive    = false;                       // a raw note takes the channel
    c.phase        = 0;
    c.phaseInc     = uint32_t((uint64_t(freqHz) << 32) / kAudioSampleRate);
    c.noise        = (wave == kWaveNoise);
    c.waveSlot     = c.noise ? 0 : uint8_t(wave & (kWavetableSlots - 1));
    c.volume       = volume > 15 ? 15 : volume;
    c.sustainLevel = kEnvUnity;
    c.releaseInc   = kEnvUnity / 256;             // ~5 ms release
    c.envLevel     = kEnvUnity;
    c.envPhase     = ApuChannel::Sustain;
}

void SoftFpga::audioNoteOff(uint8_t ch) {
    if (ch < kAudioChannels) apu_[ch].envPhase = ApuChannel::Release;
}

void SoftFpga::audioLoadWavetable(uint8_t slot, std::span<const int8_t> samples) {
    if (slot >= kWavetableSlots) return;
    const int n = std::min<int>(kWavetableLen, int(samples.size()));
    for (int i = 0; i < n; ++i) wavetables_[slot][i] = samples[i];
}

// ---- APU: sequencer (instruments + SFX patterns) --------------------------
void SoftFpga::audioSetInstrument(uint8_t id, uint8_t wave, uint8_t a, uint8_t d,
                                  uint8_t s, uint8_t r) {
    if (id >= kInstrumentSlots) return;
    instruments_[id] = { wave, a, d, s, r };
}

void SoftFpga::audioSetSfx(uint8_t id, uint8_t speed, std::span<const SfxStep> steps) {
    if (id >= kSfxSlots) return;
    Sfx& s = sfx_[id];
    s.speed = speed ? speed : 1;
    const int n = std::min<int>(kSfxSteps, int(steps.size()));
    for (int i = 0; i < kSfxSteps; ++i) s.steps[i] = (i < n) ? steps[i] : SfxStep{};
}

// Channel-stealing rule (documented, deterministic): prefer an idle channel;
// if all are busy, steal the QUIETEST (lowest volume x envelope level), and it
// resumes whatever it was doing only if re-triggered. Same in sim and RTL.
int SoftFpga::pickChannel() const {
    for (int i = 0; i < kAudioChannels; ++i)
        if (apu_[i].envPhase == ApuChannel::Idle && !apu_[i].sfxActive) return i;
    int best = 0; int64_t quietest = INT64_MAX;
    for (int i = 0; i < kAudioChannels; ++i) {
        const int64_t loud = int64_t(apu_[i].volume) * apu_[i].envLevel;
        if (loud < quietest) { quietest = loud; best = i; }
    }
    return best;
}

void SoftFpga::applySfxStep(ApuChannel& c) {
    const SfxStep& st = sfx_[c.sfxId].steps[c.sfxStep];
    if (st.vol == 0) { c.envPhase = ApuChannel::Release; return; }   // rest
    const Instrument& in = instruments_[st.inst & (kInstrumentSlots - 1)];
    const int32_t sus = int32_t(in.s) * kEnvUnity / 255;
    triggerNote(c, noteFreq_[st.note & 127], in.wave, st.vol,
                envIncForTime(in.a, kEnvUnity),
                envIncForTime(in.d, kEnvUnity - sus),
                sus,
                envIncForTime(in.r, kEnvUnity));
}

void SoftFpga::launchSfx(uint8_t id, int ch, bool musicOwned) {
    ApuChannel& c = apu_[ch];
    c.sfxActive      = true;
    c.musicOwned     = musicOwned;
    c.sfxId          = id;
    c.sfxStep        = 0;
    c.sfxCtr         = 0;
    c.sfxStepSamples = uint32_t(sfx_[id].speed) * kAudioSampleRate / 120;  // 120 ticks/s
}

void SoftFpga::audioPlaySfx(uint8_t id, int channel) {
    if (id >= kSfxSlots) return;
    const int ch = (channel >= 0 && channel < kAudioChannels) ? channel : pickChannel();
    launchSfx(id, ch, /*musicOwned*/false);   // a game SFX takes ownership of the channel
}

// ---- APU: music sequencer -------------------------------------------------
void SoftFpga::audioSetMusic(uint8_t id, std::span<const uint8_t> channelSfx, uint8_t flags) {
    if (id >= kMusicPatterns) return;
    MusicPattern& m = music_[id];
    for (int i = 0; i < kAudioChannels; ++i)
        m.sfx[i] = (i < int(channelSfx.size())) ? channelSfx[i] : 0xFF;
    m.loopStart = flags & 1; m.loopEnd = flags & 2; m.stop = flags & 4;
}

void SoftFpga::music(int track, int /*command*/) {
    if (track < 0) {                                  // stop + release music voices
        musicActive = false;
        for (auto& c : apu_)
            if (c.musicOwned) { c.sfxActive = false; c.musicOwned = false;
                                c.envPhase = ApuChannel::Release; }
        return;
    }
    if (track >= kMusicPatterns) return;
    musicActive = true;
    musicCur = track;
    musicLoopStart = track;
    musicCtr = 0;                                     // startMusicPattern fires next sample
}

// Launch every assigned SFX for the current pattern; its length is the longest
// of them (so nothing is cut mid-phrase; channels normally share a speed).
void SoftFpga::startMusicPattern() {
    const MusicPattern& p = music_[musicCur];
    if (p.loopStart) musicLoopStart = musicCur;
    uint32_t dur = 0;
    for (int ch = 0; ch < kAudioChannels; ++ch) {
        if (p.sfx[ch] == 0xFF) continue;
        launchSfx(p.sfx[ch], ch, /*musicOwned*/true);
        const uint32_t d = uint32_t(sfx_[p.sfx[ch]].speed) * kSfxSteps
                         * kAudioSampleRate / 120;
        if (d > dur) dur = d;
    }
    if (dur == 0) dur = kSfxSteps * 8u * kAudioSampleRate / 120;   // empty pattern default
    musicPatternSamples = dur;
}

int SoftFpga::pullAudio(int16_t* dst, int maxFrames) {
    for (int f = 0; f < maxFrames; ++f) {
        // Music sequencer: launch each pattern's SFX and advance/loop autonomously.
        if (musicActive) {
            if (musicCtr == 0) startMusicPattern();
            if (++musicCtr >= musicPatternSamples) {
                musicCtr = 0;
                const MusicPattern& p = music_[musicCur];
                if (p.stop)          musicActive = false;
                else if (p.loopEnd)  musicCur = musicLoopStart;
                else if (++musicCur >= kMusicPatterns) musicActive = false;
            }
        }

        int mix = 0;
        for (auto& c : apu_) {
            // SFX player: apply the step's note at each step boundary.
            if (c.sfxActive) {
                if (c.sfxCtr == 0) applySfxStep(c);
                if (++c.sfxCtr >= c.sfxStepSamples) {
                    c.sfxCtr = 0;
                    if (++c.sfxStep >= kSfxSteps) {          // pattern finished
                        c.sfxActive = false;
                        c.envPhase = ApuChannel::Release;
                    }
                }
            }

            advanceEnv(c);
            if (c.envPhase == ApuChannel::Idle && c.envLevel == 0) continue;

            c.phase += c.phaseInc;
            const uint8_t idx = uint8_t(c.phase >> 27);       // top 5 bits -> 0..31
            int sample;
            if (c.noise) {
                if (idx != c.lastIdx) {                        // clock LFSR at note rate
                    const uint16_t bit = uint16_t((c.lfsr ^ (c.lfsr >> 1)) & 1u);
                    c.lfsr = uint16_t((c.lfsr >> 1) | (bit << 14));
                    c.lastIdx = idx;
                }
                sample = (c.lfsr & 1) ? 127 : -128;
            } else {
                sample = wavetables_[c.waveSlot][idx];
            }
            // volume (0..15) x envelope (0..kEnvUnity), fixed-point.
            mix += int(int64_t(sample) * c.volume * c.envLevel >> 20);
        }
        int s = mix * 4;                                       // 4-channel headroom -> int16
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        dst[2 * f]     = int16_t(s);
        dst[2 * f + 1] = int16_t(s);
    }
    return maxFrames;
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
