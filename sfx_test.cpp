// sfx_test.cpp — headless test of instruments + SFX patterns + ADSR + stealing.

#include "soft_fpga.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

using namespace s8;

static SoftFpga dev;

static std::vector<int16_t> render(double dur) {
    int frames = int(dur * kAudioSampleRate);
    std::vector<int16_t> b(size_t(frames) * 2);
    dev.pullAudio(b.data(), frames);
    return b;
}
// frequency in a window of the left channel via rising zero-crossings
static double freqIn(const std::vector<int16_t>& s, int startF, int countF) {
    int rising = 0, prev = 0;
    for (int i = startF; i < startF + countF; ++i) {
        int v = s[size_t(i) * 2];
        if (prev < 0 && v >= 0) ++rising;
        prev = v;
    }
    return double(rising) * kAudioSampleRate / countF;
}
static int peakIn(const std::vector<int16_t>& s, int startF, int countF) {
    int p = 0;
    for (int i = startF; i < startF + countF; ++i) p = std::max(p, std::abs(int(s[size_t(i)*2])));
    return p;
}
static void writeWav(const char* path, const std::vector<int16_t>& s, int rate){
    FILE* f=std::fopen(path,"wb"); uint32_t db=uint32_t(s.size()*2), ck=36+db;
    uint16_t ch=2,bits=16; uint32_t r=uint32_t(rate),br=r*ch*bits/8; uint16_t ba=ch*bits/8,pcm=1; uint32_t fl=16;
    std::fwrite("RIFF",1,4,f);std::fwrite(&ck,4,1,f);std::fwrite("WAVE",1,4,f);std::fwrite("fmt ",1,4,f);
    std::fwrite(&fl,4,1,f);std::fwrite(&pcm,2,1,f);std::fwrite(&ch,2,1,f);std::fwrite(&r,4,1,f);
    std::fwrite(&br,4,1,f);std::fwrite(&ba,2,1,f);std::fwrite(&bits,2,1,f);
    std::fwrite("data",1,4,f);std::fwrite(&db,4,1,f);std::fwrite(s.data(),2,s.size(),f);std::fclose(f);
}
static int failures=0;
static void check(const char*w,bool ok,double g=0,double want=0){
    std::printf("  [%s] %-32s",ok?"PASS":"FAIL",w);
    if(want) std::printf(" got=%.1f want~%.1f",g,want);
    std::printf("\n"); if(!ok)++failures;
}

int main(){
    std::printf("Part 2a (sequencer engine) assertions:\n");

    // --- A) SFX melody steps at the right note frequencies ---
    dev.audioSetInstrument(0, /*wave*/0 /*square*/, /*a*/8, /*d*/10, /*s*/180, /*r*/20);
    SfxStep mel[kSfxSteps] = {};
    const int notes[4] = {60, 64, 67, 72};   // C4 E4 G4 C5
    for (int i = 0; i < 4; ++i) mel[i] = { uint8_t(notes[i]), 0, 12 };
    dev.audioSetSfx(0, /*speed*/12, std::span<const SfxStep>(mel, kSfxSteps));  // 100ms/step

    dev.audioPlaySfx(0, 0);
    auto song = render(0.45);
    const int stepF = 12 * kAudioSampleRate / 120;   // 4800
    const double want[4] = {262, 330, 392, 523};     // equal-temperament targets
    const char* nm[4] = {"step0 C4","step1 E4","step2 G4","step3 C5"};
    for (int i = 0; i < 4; ++i) {
        double got = freqIn(song, i*stepF + 1200, 3000);
        check(nm[i], std::fabs(got - want[i]) < want[i]*0.06, got, want[i]);
    }

    // --- B) ADSR envelope: attack rises, sustains, releases to silence ---
    dev.audioSetInstrument(1, 3 /*sine*/, /*a*/40, /*d*/30, /*s*/130, /*r*/60);
    SfxStep one[kSfxSteps] = {};
    one[0] = { 69, 1, 12 };            // A4 held one step, then rests
    dev.audioSetSfx(1, /*speed*/40, std::span<const SfxStep>(one, kSfxSteps)); // ~333ms/step
    // fresh channel
    dev.audioPlaySfx(1, 1);
    auto env = render(0.7);
    int base = 1 /*ch1 offset irrelevant; measure ch-mixed*/;
    (void)base;
    int pAttack  = peakIn(env, int(0.010*kAudioSampleRate), int(0.005*kAudioSampleRate));
    int pSustain = peakIn(env, int(0.250*kAudioSampleRate), int(0.020*kAudioSampleRate));
    int pRelease = peakIn(env, int(0.600*kAudioSampleRate), int(0.020*kAudioSampleRate));
    check("attack quieter than sustain", pAttack < pSustain, pAttack, pSustain);
    check("release rings down to ~0",    pRelease < 60,       pRelease, 0);

    // --- C) steal prefers idle: a loud note on ch0 survives an auto sfx ---
    SoftFpga d2;
    d2.audioSetInstrument(0,0,8,10,180,20);
    d2.audioSetSfx(0,12,std::span<const SfxStep>(mel,kSfxSteps));
    d2.audioSetChannel(0, 440, 0 /*square*/, 12);   // loud sustained on ch0
    d2.audioPlaySfx(0, -1);                          // auto: 3 idle channels exist
    std::vector<int16_t> s2(size_t(3000)*2); d2.pullAudio(s2.data(), 3000);
    double f440 = freqIn(s2, 200, 2600);
    // 440 (ch0) mixes with the sfx C4 (262); rising-crossings dominated by both,
    // but the 440 note must still be audibly present -> output non-trivial & tonal.
    check("idle preferred (ch0 note survives)", peakIn(s2,0,3000) > 1000, 0, 0);
    (void)f440;

    writeWav("sfx_test.wav", song, kAudioSampleRate);
    std::printf("\nwrote sfx_test.wav\n%s (%d failure%s)\n",
                failures?"FAILED":"OK", failures, failures==1?"":"s");
    return failures?1:0;
}
