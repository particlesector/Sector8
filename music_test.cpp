// music_test.cpp — headless test of the music sequencer (chains SFX + loops).

#include "soft_fpga.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <vector>

using namespace s8;

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
    int p = 0; for (int i=startF;i<startF+countF;++i) p=std::max(p,std::abs(int(s[size_t(i)*2])));
    return p;
}
static void writeWav(const char* path, const std::vector<int16_t>& s, int rate){
    FILE* f=std::fopen(path,"wb"); uint32_t db=uint32_t(s.size()*2), ck=36+db;
    uint16_t ch=2,bits=16,pcm=1,ba=ch*bits/8; uint32_t r=uint32_t(rate),br=r*ba,fl=16;
    std::fwrite("RIFF",1,4,f);std::fwrite(&ck,4,1,f);std::fwrite("WAVE",1,4,f);std::fwrite("fmt ",1,4,f);
    std::fwrite(&fl,4,1,f);std::fwrite(&pcm,2,1,f);std::fwrite(&ch,2,1,f);std::fwrite(&r,4,1,f);
    std::fwrite(&br,4,1,f);std::fwrite(&ba,2,1,f);std::fwrite(&bits,2,1,f);
    std::fwrite("data",1,4,f);std::fwrite(&db,4,1,f);std::fwrite(s.data(),2,s.size(),f);std::fclose(f);
}
static int failures=0;
static void check(const char*w,bool ok,double g=0,double want=0){
    std::printf("  [%s] %-34s",ok?"PASS":"FAIL",w);
    if(want) std::printf(" got=%.0f cmp=%.0f",g,want);
    std::printf("\n"); if(!ok)++failures;
}

// build the shared song data into a device
static void buildSong(SoftFpga& d) {
    d.audioSetInstrument(0, /*square*/0, /*a*/1, /*d*/0, /*s*/255, /*r*/10);
    auto held = [&](uint8_t id, int note){
        SfxStep st[kSfxSteps];
        for (auto& s : st) s = { uint8_t(note), 0, 12 };
        d.audioSetSfx(id, /*speed*/2, std::span<const SfxStep>(st, kSfxSteps));  // 25600 smp
    };
    held(0, 60);  // C4
    held(1, 67);  // G4
    held(2, 48);  // C3 (bass)
    uint8_t p0[4]={0,0xFF,0xFF,0xFF};  d.audioSetMusic(0, std::span<const uint8_t>(p0,4), 1); // loop_start
    uint8_t p1[4]={1,0xFF,0xFF,0xFF};  d.audioSetMusic(1, std::span<const uint8_t>(p1,4), 2); // loop_end
    uint8_t p2[4]={0,2,0xFF,0xFF};     d.audioSetMusic(2, std::span<const uint8_t>(p2,4), 4); // C4+C3, stop
}

int main(){
    std::printf("Part 2b (music sequencer) assertions:\n");
    const int patF = 2 * kSfxSteps * kAudioSampleRate / 120;   // 25600 samples/pattern

    // --- A) advance + loop-back: song 0(C4,loopStart) -> 1(G4,loopEnd) -> 0 ...
    {
        SoftFpga d; buildSong(d);
        d.music(0, 0);
        std::vector<int16_t> song(size_t(patF*3) * 2);
        d.pullAudio(song.data(), patF*3);
        double f0 = freqIn(song, 0*patF + 4000, 16000);   // pattern 0 -> C4 (262)
        double f1 = freqIn(song, 1*patF + 4000, 16000);   // pattern 1 -> G4 (392)
        double f2 = freqIn(song, 2*patF + 4000, 16000);   // looped back -> C4
        check("pattern 0 plays C4 (<330)",  f0 < 330, f0, 330);
        check("advances to pattern 1 G4 (>330)", f1 > 330, f1, 330);
        check("loop_end returns to C4 (<330)",   f2 < 330, f2, 330);
        writeWav("music_test.wav", song, kAudioSampleRate);
    }

    // --- B) multichannel pattern: two voices louder than one ---
    {
        SoftFpga d; buildSong(d);
        d.music(2, 0);                                    // pattern 2 = C4 + C3
        std::vector<int16_t> s(size_t(patF) * 2);
        d.pullAudio(s.data(), patF);
        int pk = peakIn(s, 0, patF);
        check("two channels sum above one voice", pk > 6500 && pk < 32767, pk, 6096);
    }

    // --- C) music(-1) stops and releases the voices ---
    {
        SoftFpga d; buildSong(d);
        d.music(0, 0);
        std::vector<int16_t> a(size_t(8000)*2); d.pullAudio(a.data(), 8000);
        d.music(-1, 0);
        std::vector<int16_t> b(size_t(4000)*2); d.pullAudio(b.data(), 4000);
        int tail = peakIn(b, 3000, 1000);
        check("stop releases to silence", tail < 100, tail, 0);
    }

    std::printf("\nwrote music_test.wav\n%s (%d failure%s)\n",
                failures?"FAILED":"OK", failures, failures==1?"":"s");
    return failures?1:0;
}
