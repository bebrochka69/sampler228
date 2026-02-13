#pragma once

#include <vector>

class SimpleFmCore {
public:
    SimpleFmCore() = default;

    void init(int sampleRate, int voices);
    void setParams(float fmAmount, float ratio, float feedback);
    void noteOn(int note, int velocity);
    void noteOff(int note);
    void render(float *outL, float *outR, int frames);

private:
    struct Voice {
        int midi = -1;
        int velocity = 0;
        bool keydown = false;
        bool active = false;
        float phaseC = 0.0f;
        float phaseM = 0.0f;
        float incC = 0.0f;
        float incM = 0.0f;
        float amp = 0.0f;
        float feedbackZ = 0.0f;
    };

    int findVoiceForNote(int note) const;
    int findFreeVoice();
    float midiToFreq(int note) const;

    int sampleRate_ = 48000;
    int voiceCursor_ = 0;
    float fmAmount_ = 0.4f;
    float ratio_ = 1.0f;
    float feedback_ = 0.0f;
    std::vector<Voice> voices_;
};
