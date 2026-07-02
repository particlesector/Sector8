// host/host_sdl3.cpp  — Sector8 SDL3 host shell.

#include "host_sdl3.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace s8 {

bool Sdl3Host::init(const char* title, int winW, int winH) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    window_ = SDL_CreateWindow(title, winW, winH, SDL_WINDOW_RESIZABLE);
    if (!window_) { SDL_Log("CreateWindow: %s", SDL_GetError()); return false; }

    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) { SDL_Log("CreateRenderer: %s", SDL_GetError()); return false; }
    SDL_SetRenderVSync(renderer_, 0);   // pacer is authoritative, not vsync

    tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA8888,
                             SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);
    if (!tex_) { SDL_Log("CreateTexture: %s", SDL_GetError()); return false; }
    SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_NEAREST);   // crisp pixels

    // Audio: open now, feed silence until the APU lands (M3).
    SDL_AudioSpec spec; SDL_zero(spec);
    spec.format = SDL_AUDIO_S16; spec.channels = 2; spec.freq = 48000;
    audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                       &spec, nullptr, nullptr);
    if (audio_) SDL_ResumeAudioStreamDevice(audio_);
    else SDL_Log("audio open failed (continuing silent): %s", SDL_GetError());

    // Open up to kMaxPlayers gamepads already attached.
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) {
        for (int i = 0, slot = 0; i < n && slot < kMaxPlayers; ++i)
            if (SDL_Gamepad* g = SDL_OpenGamepad(ids[i])) pads_[slot++] = g;
        SDL_free(ids);
    }
    nextTickNs_ = 0;
    return true;
}

void Sdl3Host::readKeyboard(PadState& p) {
    const bool* k = SDL_GetKeyboardState(nullptr);   // SDL3: const bool*
    auto set = [&](Button b, SDL_Scancode sc) { if (k[sc]) p.set(b, true); };
    set(Button::Left,  SDL_SCANCODE_LEFT);
    set(Button::Right, SDL_SCANCODE_RIGHT);
    set(Button::Up,    SDL_SCANCODE_UP);
    set(Button::Down,  SDL_SCANCODE_DOWN);
    set(Button::A,     SDL_SCANCODE_Z);
    set(Button::B,     SDL_SCANCODE_X);
    set(Button::X,     SDL_SCANCODE_A);
    set(Button::Y,     SDL_SCANCODE_S);
    set(Button::Start, SDL_SCANCODE_RETURN);
    set(Button::Select,SDL_SCANCODE_RSHIFT);
}

void Sdl3Host::readGamepads(InputState& io) {
    for (int slot = 0; slot < kMaxPlayers; ++slot) {
        SDL_Gamepad* g = pads_[slot];
        if (!g) continue;
        auto set = [&](Button b, SDL_GamepadButton gb) {
            if (SDL_GetGamepadButton(g, gb)) io.pad[slot].set(b, true);
        };
        set(Button::Up,    SDL_GAMEPAD_BUTTON_DPAD_UP);
        set(Button::Down,  SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        set(Button::Left,  SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        set(Button::Right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        set(Button::A,     SDL_GAMEPAD_BUTTON_SOUTH);
        set(Button::B,     SDL_GAMEPAD_BUTTON_EAST);
        set(Button::X,     SDL_GAMEPAD_BUTTON_WEST);
        set(Button::Y,     SDL_GAMEPAD_BUTTON_NORTH);
        set(Button::Start, SDL_GAMEPAD_BUTTON_START);
        set(Button::Select,SDL_GAMEPAD_BUTTON_BACK);
    }
}

bool Sdl3Host::processEvents(InputState& io) {
    bool running = true;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            running = false;
        } else if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            for (int s = 0; s < kMaxPlayers; ++s)
                if (!pads_[s]) { pads_[s] = SDL_OpenGamepad(e.gdevice.which); break; }
        } else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            for (int s = 0; s < kMaxPlayers; ++s)
                if (pads_[s] && SDL_GetGamepadID(pads_[s]) == e.gdevice.which) {
                    SDL_CloseGamepad(pads_[s]); pads_[s] = nullptr;
                }
        }
    }
    io = InputState{};
    readKeyboard(io.pad[0]);   // keyboard always drives player 1
    readGamepads(io);          // gamepads overlay their slots
    return running;
}

void Sdl3Host::present(const VideoFrame& frame) {
    SDL_UpdateTexture(tex_, nullptr, frame.pixels,
                      frame.width * int(sizeof(uint32_t)));

    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(renderer_, &ow, &oh);
    int scale = std::min(ow / frame.width, oh / frame.height);
    if (scale < 1) scale = 1;

    SDL_FRect dst;
    dst.w = float(frame.width  * scale);
    dst.h = float(frame.height * scale);
    dst.x = float((ow - int(dst.w)) / 2);
    dst.y = float((oh - int(dst.h)) / 2);

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);                       // letterbox = black
    SDL_RenderTexture(renderer_, tex_, nullptr, &dst);
    SDL_RenderPresent(renderer_);
}

int Sdl3Host::pushAudio(const int16_t* stereo, int frames) {
    if (!audio_ || frames <= 0) return 0;
    SDL_PutAudioStreamData(audio_, stereo, frames * 2 * int(sizeof(int16_t)));
    return frames;
}

void Sdl3Host::waitForNextFrame() {
    const uint64_t frameNs = SDL_NS_PER_SECOND / 60;
    const uint64_t now = SDL_GetTicksNS();
    if (nextTickNs_ == 0) nextTickNs_ = now + frameNs;
    if (now < nextTickNs_) SDL_DelayNS(nextTickNs_ - now);
    nextTickNs_ += frameNs;
    const uint64_t after = SDL_GetTicksNS();
    if (nextTickNs_ + frameNs < after) nextTickNs_ = after + frameNs;  // resync
}

void Sdl3Host::shutdown() {
    for (auto& g : pads_) { if (g) SDL_CloseGamepad(g); g = nullptr; }
    if (audio_)    SDL_DestroyAudioStream(audio_);
    if (tex_)      SDL_DestroyTexture(tex_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    SDL_Quit();
}

} // namespace s8
