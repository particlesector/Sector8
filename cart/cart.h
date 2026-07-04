// cart/cart.h
//
// Sector8 cart container: a versioned, section-based binary. The reader here is
// ARM-safe (no exceptions, no heap) because the RP2350 firmware parses the same
// bytes off the SD card. The writer lives host-side in the packer.
//
// Layout:
//   [ 'S','8','C','1' ]
//   [ formatVersion u16 ] [ specVersion u16 ] [ sectionCount u16 ] [ reserved u16 ]
//   repeated sectionCount times:
//     [ type u16 ] [ reserved u16 ] [ length u32 ] [ crc32 u32 ] [ data (length) ]
//   (all little-endian)

#pragma once
#include <cstdint>
#include <cstddef>

namespace s8 {

inline constexpr uint8_t  kCartMagic[4]      = { 'S', '8', 'C', '1' };
inline constexpr uint16_t kCartFormatVersion = 1;
inline constexpr int      kCartHeaderBytes   = 4 + 2 + 2 + 2 + 2;   // 12
inline constexpr int      kSectionHeaderBytes = 2 + 2 + 4 + 4;      // 12

enum class CartSection : uint16_t {
    Meta = 1, Bytecode = 2, Palette = 3, Tiles = 4, Map0 = 5, Map1 = 6,
    Instruments = 7, Sfx = 8, Music = 9,
};

// CRC-32 (IEEE 802.3, reflected). Small bitwise implementation.
inline uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        c ^= data[i];
        for (int k = 0; k < 8; ++k)
            c = (c >> 1) ^ (0xEDB88320u & (~(c & 1u) + 1u));
    }
    return c ^ 0xFFFFFFFFu;
}

// --- little-endian readers ---
inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// A tilemap cell packs into 16 bits: tile[0..9] palette[10..11] hFlip[12] vFlip[13] prio[14].
inline uint16_t packCell(uint16_t tile, uint8_t pal, bool h, bool v, bool prio) {
    return uint16_t((tile & 0x3FF) | (uint16_t(pal & 3) << 10)
                    | (uint16_t(h) << 12) | (uint16_t(v) << 13) | (uint16_t(prio) << 14));
}

// Non-owning view over a cart in memory.
struct CartView {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    uint16_t       formatVersion = 0, specVersion = 0, sectionCount = 0;
    bool           valid = false;

    // Locate a section by type; returns its (crc-verified) payload. False if absent/corrupt.
    bool section(CartSection type, const uint8_t*& out, uint32_t& len) const {
        if (!valid) return false;
        const uint8_t* p = data + kCartHeaderBytes;
        const uint8_t* end = data + size;
        for (uint16_t i = 0; i < sectionCount; ++i) {
            if (p + kSectionHeaderBytes > end) return false;
            const uint16_t t   = rd16(p);
            const uint32_t l   = rd32(p + 4);
            const uint32_t crc = rd32(p + 8);
            const uint8_t* body = p + kSectionHeaderBytes;
            if (body + l > end) return false;
            if (CartSection(t) == type) {
                if (crc32(body, l) != crc) return false;   // corrupt
                out = body; len = l; return true;
            }
            p = body + l;
        }
        return false;
    }
};

inline CartView parseCart(const uint8_t* data, size_t size) {
    CartView v;
    if (size < size_t(kCartHeaderBytes)) return v;
    for (int i = 0; i < 4; ++i) if (data[i] != kCartMagic[i]) return v;
    v.data = data; v.size = size;
    v.formatVersion = rd16(data + 4);
    v.specVersion   = rd16(data + 6);
    v.sectionCount  = rd16(data + 8);
    v.valid = (v.formatVersion <= kCartFormatVersion);
    return v;
}

} // namespace s8
