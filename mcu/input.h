// mcu/input.h
//
// Sector8 — input state shared between the host (which fills it from a gamepad
// or keyboard) and s8_mcu's button()/button_pressed(). Input is MCU-local in
// the real machine (§3.8), so this lives on the MCU side; the host just writes it.

#pragma once
#include <cstdint>

namespace s8 {

enum class Button : uint8_t {
    Left, Right, Up, Down, A, B, X, Y, Start, Select, Count
};

inline constexpr int kMaxPlayers = 2;   // baseline (§3.8); 4 is a capability tier

struct PadState {
    uint16_t held = 0;
    bool down(Button b) const { return (held >> int(b)) & 1u; }
    void set(Button b, bool v) {
        const uint16_t m = uint16_t(1u << int(b));
        held = v ? uint16_t(held | m) : uint16_t(held & ~m);
    }
};

struct InputState {
    PadState pad[kMaxPlayers];
};

} // namespace s8
