// host/host.h
//
// Sector8 — the host shell seam. Window, input, audio sink, and frame pacing
// live behind this so the console core never depends on SDL. Sdl3Host is the
// real implementation; a headless NullHost (for golden tests / the tiler)
// implements the same interface.

#pragma once
#include "s8_types.h"
#include "input.h"

namespace s8 {

class HostPlatform {
public:
    virtual ~HostPlatform() = default;

    // Create window/renderer/audio. Returns false on failure.
    virtual bool init(const char* title, int winW, int winH) = 0;

    // Pump OS/input events, fill `io` for this frame. Returns false on quit.
    virtual bool processEvents(InputState& io) = 0;

    // Upload the 320x180 frame, integer-scale + letterbox, present.
    virtual void present(const VideoFrame& frame) = 0;

    // Queue `frames` interleaved stereo S16 samples; returns frames accepted.
    virtual int pushAudio(const int16_t* stereo, int frames) = 0;

    // Block until the next 60 Hz boundary — this is the sim's "vsync".
    virtual void waitForNextFrame() = 0;

    virtual void shutdown() = 0;
};

} // namespace s8
