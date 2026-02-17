#pragma once

#include <memory>

class VitalCore {
public:
    struct Params {
        float fmAmount = 0.4f;
        float ratio = 1.0f;
        float feedback = 0.0f;
        float cutoff = 0.8f;
        float resonance = 0.1f;
        int filterType = 0;
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
        float attack = 0.15f;
        float decay = 0.25f;
        float sustain = 0.7f;
        float release = 0.25f;
    };

    VitalCore();
    ~VitalCore();

    void init(int sampleRate, int voices);
    void setParams(const Params &params, float bpm);
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void render(float *outL, float *outR, int frames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
