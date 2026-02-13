#pragma once

#include <cstdint>
#include <vector>

class SimpleFmCore {
public:
    struct Params {
        float fmAmount = 0.4f;
        float ratio = 1.0f;
        float feedback = 0.0f;
        int osc1Wave = 0;
        int osc2Wave = 1;
        int osc1Voices = 1;
        int osc2Voices = 1;
        float osc1Detune = 0.0f;
        float osc2Detune = 0.0f;
        float osc1Gain = 0.8f;
        float osc2Gain = 0.6f;
        float osc1Pan = -0.1f;
        float osc2Pan = 0.1f;
    };

    SimpleFmCore() = default;

    void init(int sampleRate, int voices);
    void setParams(const Params &params);
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void render(float *outL, float *outR, int frames);

private:
    struct Voice {
        int midi = -1;
        int velocity = 0;
        bool keydown = false;
        bool active = false;
        float baseFreq = 0.0f;
        float phase1[8]{};
        float phase2[8]{};
        float inc1[8]{};
        float inc2[8]{};
        float amp = 0.0f;
        float feedbackZ = 0.0f;
        uint32_t noise = 0x1234567u;
    };

    int findVoiceForNote(int note) const;
    int findFreeVoice();
    float midiToFreq(int note) const;
    void updateVoiceIncrements(Voice &voice);
    float oscWave(int wave, float phase, Voice &voice) const;
    void computeDetuneOffsets(int voices, float detune, float *out) const;

    int sampleRate_ = 48000;
    int voiceCursor_ = 0;
    Params params_;
    float detune1_[8]{};
    float detune2_[8]{};
    float osc1GainL_ = 0.8f;
    float osc1GainR_ = 0.8f;
    float osc2GainL_ = 0.6f;
    float osc2GainR_ = 0.6f;
    std::vector<Voice> voices_;
};
