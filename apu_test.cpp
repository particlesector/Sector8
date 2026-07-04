// apu_test.cpp — headless test of the 4-channel wavetable synth.

#include "soft_fpga.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace s8;

static SoftFpga dev;

// Render `dur` seconds from channel state into a fresh stereo buffer.
static std::vector<int16_t> render(double dur) {
    int frames = int(dur * kAudioSampleRate);
    std::vector<int16_t> buf(size_t(frames) * 2);
    dev.pullAudio(buf.data(), frames);
    return buf;
}

struct Ana { double freq; int peak; int signChanges; };
static Ana analyze(const std::vector<int16_t>& s, double dur) {
    int rising = 0, changes = 0, peak = 0, prev = 0;
    for (size_t i = 0; i < s.size(); i += 2) {          // left channel
        int v = s[i];
        if (std::abs(v) > peak) peak = std::abs(v);
        if ((prev < 0) != (v < 0)) ++changes;
        if (prev < 0 && v >= 0) ++rising;
        prev = v;
    }
    return { rising / dur, peak, changes };
}

static void writeWav(const char* path, const std::vector<int16_t>& s, int rate) {
    FILE* f = std::fopen(path, "wb");
    uint32_t dataBytes = uint32_t(s.size() * 2);
    uint32_t chunk = 36 + dataBytes;
    uint16_t ch = 2, bits = 16; uint32_t r = uint32_t(rate);
    uint32_t byteRate = r * ch * bits / 8; uint16_t blockAlign = ch * bits / 8;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&chunk, 4, 1, f);
    std::fwrite("WAVE", 1, 4, f); std::fwrite("fmt ", 1, 4, f);
    uint32_t fmtLen = 16; uint16_t pcm = 1;
    std::fwrite(&fmtLen,4,1,f); std::fwrite(&pcm,2,1,f); std::fwrite(&ch,2,1,f);
    std::fwrite(&r,4,1,f); std::fwrite(&byteRate,4,1,f);
    std::fwrite(&blockAlign,2,1,f); std::fwrite(&bits,2,1,f);
    std::fwrite("data",1,4,f); std::fwrite(&dataBytes,4,1,f);
    std::fwrite(s.data(), 2, s.size(), f);
    std::fclose(f);
}

static int failures = 0;
static void check(const char* w, bool ok, double got=0, double want=0) {
    std::printf("  [%s] %-34s", ok?"PASS":"FAIL", w);
    if (want) std::printf(" got=%.1f want~%.1f", got, want);
    std::printf("\n"); if(!ok) ++failures;
}

int main() {
    std::vector<int16_t> song;   // accumulate for the WAV
    auto play = [&](int ch,int freq,int wave,int vol,double dur){
        dev.audioSetChannel(uint8_t(ch),uint16_t(freq),uint8_t(wave),uint8_t(vol));
        auto seg = render(dur);
        song.insert(song.end(), seg.begin(), seg.end());
        return seg;
    };
    auto silence = [&](double dur){
        for(int c=0;c<kAudioChannels;++c) dev.audioNoteOff(uint8_t(c));
        auto seg = render(dur);
        song.insert(song.end(), seg.begin(), seg.end());
    };

    std::printf("APU assertions:\n");
    const double D = 0.4;

    auto sq  = play(0,440,0,12,D); silence(0.12);   // square
    auto tri = play(0,440,1,12,D); silence(0.12);   // triangle
    auto saw = play(0,440,2,12,D); silence(0.12);   // saw
    auto sin_= play(0,440,3,12,D); silence(0.12);   // sine

    Ana a;
    a = analyze(sq,  D); check("square 440Hz",   std::abs(a.freq-440)<22, a.freq,440);
    a = analyze(tri, D); check("triangle 440Hz", std::abs(a.freq-440)<22, a.freq,440);
    a = analyze(saw, D); check("saw 440Hz",      std::abs(a.freq-440)<22, a.freq,440);
    Ana as = analyze(sin_,D); check("sine 440Hz",std::abs(as.freq-440)<22, as.freq,440);
    check("sine non-silent, no clip", as.peak>1000 && as.peak<32767);

    // Noise: broadband -> far more sign changes than a 440 tone.
    auto nz = play(0,3000,kWaveNoise,12,D); silence(0.12);
    Ana an = analyze(nz, D);
    Ana asq = analyze(sq, D);
    check("noise is broadband", an.signChanges > 5*asq.signChanges,
          an.signChanges, asq.signChanges);

    // Envelope release: gate off -> tail ramps to ~0.
    dev.audioSetChannel(0,330,0,15); render(0.1);
    dev.audioNoteOff(0);
    auto tail = render(0.05);   // >> kEnvMax samples
    int lastPeak=0; for(size_t i=tail.size()-64; i<tail.size(); i+=2) lastPeak=std::max(lastPeak,std::abs(int(tail[i])));
    check("release ramps to silence", lastPeak < 50, lastPeak, 0);

    // Chord: 3 channels sum louder than one; no clip.
    dev.audioSetChannel(0,262,3,10);  // C4 sine
    dev.audioSetChannel(1,330,3,10);  // E4
    dev.audioSetChannel(2,392,3,10);  // G4
    auto chord = render(0.8);
    song.insert(song.end(), chord.begin(), chord.end());
    Ana ac = analyze(chord, 0.8);
    check("chord louder than 1 voice", ac.peak > as.peak, ac.peak, as.peak);
    check("chord does not clip", ac.peak < 32767, ac.peak, 0);
    silence(0.2);

    writeWav("apu_test.wav", song, kAudioSampleRate);
    std::printf("\nwrote apu_test.wav (%.1f s)\n", song.size()/2.0/kAudioSampleRate);
    std::printf("%s (%d failure%s)\n", failures?"FAILED":"OK", failures, failures==1?"":"s");
    return failures?1:0;
}
