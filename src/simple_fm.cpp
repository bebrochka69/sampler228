#include "simple_fm.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kTwoPi = 6.283185307179586f;
}

void SimpleFmCore::init(int sampleRate, int voices) {
    if (sampleRate <= 0) {
        sampleRate = 48000;
    }
    if (voices <= 0) {
        voices = 8;
    }
    sampleRate_ = sampleRate;
    voiceCursor_ = 0;
    voices_.assign(static_cast<size_t>(voices), Voice{});
}

void SimpleFmCore::setParams(float fmAmount, float ratio, float feedback) {
    fmAmount_ = std::max(0.0f, fmAmount);
    ratio_ = std::max(0.01f, ratio);
    feedback_ = std::max(0.0f, feedback);
}

int SimpleFmCore::findVoiceForNote(int note) const {
    for (size_t i = 0; i < voices_.size(); ++i) {
        const auto &voice = voices_[i];
        if (voice.active && voice.midi == note) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int SimpleFmCore::findFreeVoice() {
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].active) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float SimpleFmCore::midiToFreq(int note) const {
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

void SimpleFmCore::noteOn(int note, int velocity) {
    if (voices_.empty()) {
        return;
    }
    if (velocity <= 0) {
        noteOff(note);
        return;
    }
    int index = findVoiceForNote(note);
    if (index < 0) {
        index = findFreeVoice();
    }
    if (index < 0) {
        index = voiceCursor_++ % static_cast<int>(voices_.size());
    }
    if (index < 0 || index >= static_cast<int>(voices_.size())) {
        return;
    }

    Voice &voice = voices_[static_cast<size_t>(index)];
    const float freq = midiToFreq(note);
    voice.midi = note;
    voice.velocity = velocity;
    voice.keydown = true;
    voice.active = true;
    voice.phaseC = 0.0f;
    voice.phaseM = 0.0f;
    voice.feedbackZ = 0.0f;
    voice.amp = static_cast<float>(velocity) / 127.0f;
    voice.incC = kTwoPi * freq / static_cast<float>(sampleRate_);
    voice.incM = kTwoPi * (freq * ratio_) / static_cast<float>(sampleRate_);
}

void SimpleFmCore::noteOff(int note) {
    for (auto &voice : voices_) {
        if (voice.active && voice.midi == note) {
            voice.keydown = false;
        }
    }
}

void SimpleFmCore::render(float *outL, float *outR, int frames) {
    if (!outL || !outR || frames <= 0) {
        return;
    }
    std::fill(outL, outL + frames, 0.0f);
    std::fill(outR, outR + frames, 0.0f);
    if (voices_.empty()) {
        return;
    }

    const float releaseStep = 1.0f / (0.08f * static_cast<float>(sampleRate_));

    for (int i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (auto &voice : voices_) {
            if (!voice.active) {
                continue;
            }
            if (!voice.keydown) {
                voice.amp -= releaseStep;
                if (voice.amp <= 0.0f) {
                    voice.active = false;
                    continue;
                }
            }
            const float mod = std::sin(voice.phaseM + feedback_ * voice.feedbackZ);
            voice.feedbackZ = mod;
            const float car = std::sin(voice.phaseC + fmAmount_ * mod);
            sum += car * voice.amp;

            voice.phaseC += voice.incC;
            if (voice.phaseC >= kTwoPi) {
                voice.phaseC -= kTwoPi;
            }
            voice.phaseM += voice.incM;
            if (voice.phaseM >= kTwoPi) {
                voice.phaseM -= kTwoPi;
            }
        }
        outL[i] = sum;
        outR[i] = sum;
    }
}
