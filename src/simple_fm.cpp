#include "simple_fm.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float clampPan(float v) {
    return std::max(-1.0f, std::min(1.0f, v));
}

void computePan(float pan, float gain, float &left, float &right) {
    pan = clampPan(pan);
    const float l = pan <= 0.0f ? 1.0f : 1.0f - pan;
    const float r = pan >= 0.0f ? 1.0f : 1.0f + pan;
    left = gain * l;
    right = gain * r;
}
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

void SimpleFmCore::setParams(const Params &params) {
    params_ = params;
    params_.fmAmount = std::max(0.0f, params_.fmAmount);
    params_.ratio = std::max(0.01f, params_.ratio);
    params_.feedback = std::max(0.0f, params_.feedback);

    params_.osc1Voices = std::max(1, std::min(8, params_.osc1Voices));
    params_.osc2Voices = std::max(1, std::min(8, params_.osc2Voices));
    params_.osc1Detune = clamp01(params_.osc1Detune);
    params_.osc2Detune = clamp01(params_.osc2Detune);
    params_.osc1Gain = clamp01(params_.osc1Gain);
    params_.osc2Gain = clamp01(params_.osc2Gain);
    params_.osc1Pan = clampPan(params_.osc1Pan);
    params_.osc2Pan = clampPan(params_.osc2Pan);

    computeDetuneOffsets(params_.osc1Voices, params_.osc1Detune, detune1_);
    computeDetuneOffsets(params_.osc2Voices, params_.osc2Detune, detune2_);

    computePan(params_.osc1Pan, params_.osc1Gain, osc1GainL_, osc1GainR_);
    computePan(params_.osc2Pan, params_.osc2Gain, osc2GainL_, osc2GainR_);

    for (auto &voice : voices_) {
        if (voice.active) {
            updateVoiceIncrements(voice);
        }
    }
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

void SimpleFmCore::computeDetuneOffsets(int voices, float detune, float *out) const {
    if (!out) {
        return;
    }
    const float detuneSemis = detune * 0.5f;
    if (voices <= 1) {
        out[0] = 0.0f;
        return;
    }
    const float center = (voices - 1) * 0.5f;
    for (int i = 0; i < voices; ++i) {
        const float spread = (static_cast<float>(i) - center) / center;
        out[i] = spread * detuneSemis;
    }
}

void SimpleFmCore::updateVoiceIncrements(Voice &voice) {
    const float base = voice.baseFreq;
    for (int i = 0; i < params_.osc1Voices; ++i) {
        const float freq = base * std::pow(2.0f, detune1_[i] / 12.0f);
        voice.inc1[i] = kTwoPi * freq / static_cast<float>(sampleRate_);
    }
    for (int i = 0; i < params_.osc2Voices; ++i) {
        const float freq = base * params_.ratio * std::pow(2.0f, detune2_[i] / 12.0f);
        voice.inc2[i] = kTwoPi * freq / static_cast<float>(sampleRate_);
    }
}

float SimpleFmCore::oscWave(int wave, float phase, Voice &voice) const {
    while (phase >= kTwoPi) {
        phase -= kTwoPi;
    }
    while (phase < 0.0f) {
        phase += kTwoPi;
    }
    const float t = phase / kTwoPi;
    switch (wave) {
        case 0: // sine
            return std::sin(phase);
        case 1: // saw
            return 2.0f * (t - 0.5f);
        case 2: // square
            return std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
        case 3: // tri
            return 1.0f - 4.0f * std::fabs(t - 0.5f);
        case 4: { // noise
            voice.noise = 1664525u * voice.noise + 1013904223u;
            return (static_cast<int>(voice.noise >> 8) & 0xFFFF) / 32768.0f - 1.0f;
        }
        case 5: // PWM
            return (t < 0.3f) ? 1.0f : -1.0f;
        case 6: { // supersaw (3 detuned saws)
            const float s1 = 2.0f * (t - 0.5f);
            const float t2 = std::fmod(t + 0.01f, 1.0f);
            const float t3 = std::fmod(t - 0.01f + 1.0f, 1.0f);
            const float s2 = 2.0f * (t2 - 0.5f);
            const float s3 = 2.0f * (t3 - 0.5f);
            return (s1 + s2 + s3) * 0.333f;
        }
        case 7: // bell
            return std::sin(phase) + 0.5f * std::sin(phase * 2.0f);
        case 8: // formant
            return std::sin(phase) + 0.5f * std::sin(phase * 3.0f);
        case 9: // metal
            return std::sin(phase) + 0.7f * std::sin(phase * 5.0f);
        default:
            return std::sin(phase);
    }
}

void SimpleFmCore::noteOn(int note, int velocity) {
    if (voices_.empty()) {
        return;
    }
    if (velocity <= 0) {
        noteOff(note);
        return;
    }
    int index = findFreeVoice();
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
    voice.baseFreq = freq;
    voice.feedbackZ = 0.0f;
    voice.amp = static_cast<float>(velocity) / 127.0f;
    for (int i = 0; i < 8; ++i) {
        voice.phase1[i] = 0.0f;
        voice.phase2[i] = 0.0f;
    }
    updateVoiceIncrements(voice);
}

void SimpleFmCore::noteOff(int note) {
    for (auto &voice : voices_) {
        if (voice.active && voice.keydown && voice.midi == note) {
            voice.keydown = false;
            break;
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
        float sumL = 0.0f;
        float sumR = 0.0f;
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

            float modSum = 0.0f;
            float osc2Sum = 0.0f;
            for (int u = 0; u < params_.osc2Voices; ++u) {
                float wave = oscWave(params_.osc2Wave, voice.phase2[u], voice);
                const float mod = wave + params_.feedback * voice.feedbackZ;
                voice.feedbackZ = mod;
                osc2Sum += wave;
                modSum += mod;
                voice.phase2[u] += voice.inc2[u];
                if (voice.phase2[u] >= kTwoPi) {
                    voice.phase2[u] -= kTwoPi;
                }
            }
            const float modSignal = (params_.osc2Voices > 0) ? (modSum / params_.osc2Voices) : 0.0f;

            float osc1Sum = 0.0f;
            for (int u = 0; u < params_.osc1Voices; ++u) {
                float wave = oscWave(params_.osc1Wave, voice.phase1[u] + params_.fmAmount * modSignal, voice);
                osc1Sum += wave;
                voice.phase1[u] += voice.inc1[u];
                if (voice.phase1[u] >= kTwoPi) {
                    voice.phase1[u] -= kTwoPi;
                }
            }

            const float osc1Out = (params_.osc1Voices > 0) ? (osc1Sum / params_.osc1Voices) : 0.0f;
            const float osc2Out = (params_.osc2Voices > 0) ? (osc2Sum / params_.osc2Voices) : 0.0f;

            const float left = osc1Out * osc1GainL_ + osc2Out * osc2GainL_;
            const float right = osc1Out * osc1GainR_ + osc2Out * osc2GainR_;

            sumL += left * voice.amp;
            sumR += right * voice.amp;
        }
        outL[i] = sumL;
        outR[i] = sumR;
    }
}
