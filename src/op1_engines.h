#pragma once

#include <array>
#include <cstdint>
#include <memory>

struct Op1Params {
    float fmAmount = 0.4f;
    float ratio = 1.0f;
    float feedback = 0.0f;
    int octave = 0;
    float cutoff = 0.8f;
    float resonance = 0.1f;
    float filterEnv = 0.0f;
    int filterType = 0;
    float lfoRate = 0.2f;
    float lfoDepth = 0.0f;
    int osc1Wave = 0;
    int osc2Wave = 1;
    int osc1Voices = 1;
    int osc2Voices = 1;
    float osc1Detune = 0.0f;
    float osc2Detune = 0.0f;
    float osc1Gain = 0.8f;
    float osc2Gain = 0.6f;
    float osc1Pan = 0.0f;
    float osc2Pan = 0.0f;
    float attack = 0.1f;
    float decay = 0.2f;
    float sustain = 0.7f;
    float release = 0.2f;
};

enum class Op1EngineType {
    Cluster,
    Digital,
    DNA,
    DrWave,
    DSynth,
    FM,
    Pulse,
    Phase,
    Ring,
    String,
    Saw,
    Voltage
};

class Op1Engine {
public:
    virtual ~Op1Engine() = default;
    virtual void init(int sampleRate, int voices) = 0;
    virtual void setParams(const Op1Params &params) = 0;
    virtual void noteOn(int note, int velocity) = 0;
    virtual void noteOff(int note) = 0;
    virtual void render(float *outL, float *outR, int frames) = 0;
};

std::unique_ptr<Op1Engine> createOp1Engine(Op1EngineType type);
