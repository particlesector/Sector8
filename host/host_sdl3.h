// host/host_sdl3.h
//
// SDL3 implementation of HostPlatform. SDL types are forward-declared here so
// that translation units using the host (e.g. app/main.cpp) don't pull in
// <SDL3/SDL.h>; only host_sdl3.cpp does.

#pragma once
#include "host.h"

extern "C" {
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Gamepad;
struct SDL_AudioStream;
}

namespace s8 {

class Sdl3Host final : public HostPlatform {
public:
    bool init(const char* title, int winW, int winH) override;
    bool processEvents(InputState& io) override;
    void present(const VideoFrame& frame) override;
    int  pushAudio(const int16_t* stereo, int frames) override;
    void waitForNextFrame() override;
    void shutdown() override;

private:
    SDL_Window*      window_   = nullptr;
    SDL_Renderer*    renderer_ = nullptr;
    SDL_Texture*     tex_      = nullptr;
    SDL_AudioStream* audio_    = nullptr;
    SDL_Gamepad*     pads_[kMaxPlayers] = { nullptr, nullptr };
    uint64_t         nextTickNs_ = 0;

    void readKeyboard(PadState& p);
    void readGamepads(InputState& io);
};

} // namespace s8
