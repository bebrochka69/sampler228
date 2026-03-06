#include "op1_engines.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr float kTwoPi = 6.28318530717958647692f;

float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

float clampRange(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

float midiToFreq(int note) {
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

float oscWave(int wave, float phase, uint32_t &noise) {
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
            noise = 1664525u * noise + 1013904223u;
            return (static_cast<int>(noise >> 8) & 0xFFFF) / 32768.0f - 1.0f;
        }
        case 5: // pwm-ish
            return (t < 0.3f) ? 1.0f : -1.0f;
        case 6: { // supersaw
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

struct Op1Voice {
    int midi = -1;
    int velocity = 0;
    bool keydown = false;
    bool active = false;
    float baseFreq = 0.0f;
    float phase1 = 0.0f;
    float phase2 = 0.0f;
    float phase3 = 0.0f;
    float amp = 0.0f;
    uint32_t noise = 0x1234567u;
    float drift = 0.0f;
    float last = 0.0f;

    struct Grain {
        float phase = 0.0f;
        float freq = 0.0f;
        float age = 0.0f;
        float dur = 0.0f;
        float amp = 0.0f;
    };
    std::array<Grain, 6> grains{};

    std::vector<float> delay;
    int delayIdx = 0;
    float delayFilter = 0.0f;
};

class Op1EngineBase : public Op1Engine {
public:
    void init(int sampleRate, int voices) override {
        if (sampleRate <= 0) {
            sampleRate = 48000;
        }
        if (voices <= 0) {
            voices = 8;
        }
        sampleRate_ = sampleRate;
        voiceCursor_ = 0;
        voices_.assign(static_cast<size_t>(voices), Op1Voice{});
    }

    void setParams(const Op1Params &params) override {
        params_ = params;
        params_.fmAmount = clamp01(params_.fmAmount);
        params_.ratio = std::max(0.1f, params_.ratio);
        params_.feedback = clamp01(params_.feedback);
        params_.octave = std::max(-4, std::min(4, params_.octave));
        params_.cutoff = clamp01(params_.cutoff);
        params_.resonance = clamp01(params_.resonance);
        params_.filterEnv = clamp01(params_.filterEnv);
        params_.lfoRate = clamp01(params_.lfoRate);
        params_.lfoDepth = clamp01(params_.lfoDepth);
        params_.osc1Voices = std::max(1, std::min(8, params_.osc1Voices));
        params_.osc2Voices = std::max(1, std::min(8, params_.osc2Voices));
        params_.osc1Detune = clamp01(params_.osc1Detune);
        params_.osc2Detune = clamp01(params_.osc2Detune);
        params_.osc1Gain = clamp01(params_.osc1Gain);
        params_.osc2Gain = clamp01(params_.osc2Gain);
    }

    void noteOn(int note, int velocity) override {
        if (voices_.empty()) {
            return;
        }
        if (velocity <= 0) {
            noteOff(note);
            return;
        }
        int idx = findFreeVoice();
        if (idx < 0) {
            idx = voiceCursor_++ % static_cast<int>(voices_.size());
        }
        if (idx < 0 || idx >= static_cast<int>(voices_.size())) {
            return;
        }

        Op1Voice &voice = voices_[static_cast<size_t>(idx)];
        voice.midi = note;
        voice.velocity = velocity;
        voice.keydown = true;
        voice.active = true;
        const float freq = midiToFreq(note + params_.octave * 12);
        voice.baseFreq = freq;
        voice.amp = static_cast<float>(velocity) / 127.0f;
        voice.noise = 1664525u * voice.noise + 1013904223u;
        voice.phase1 = 0.0f;
        voice.phase2 = 0.0f;
        voice.phase3 = 0.0f;
        voice.last = 0.0f;
        onNoteOn(voice);
    }

    void noteOff(int note) override {
        for (auto &voice : voices_) {
            if (voice.active && voice.keydown && voice.midi == note) {
                voice.keydown = false;
                onNoteOff(voice);
                break;
            }
        }
    }

    void render(float *outL, float *outR, int frames) override {
        if (!outL || !outR || frames <= 0) {
            return;
        }
        std::fill(outL, outL + frames, 0.0f);
        std::fill(outR, outR + frames, 0.0f);
        if (voices_.empty()) {
            return;
        }

        const float releaseSec = 0.06f;
        const float releaseStep =
            (releaseSec > 0.0f) ? (1.0f / (releaseSec * static_cast<float>(sampleRate_))) : 1.0f;
        const float lfo = 0.0f;

        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;

            for (auto &voice : voices_) {
                if (!voice.active) {
                    continue;
                }
                if (!voice.keydown) {
                    voice.amp -= releaseStep;
                    if (voice.amp <= 0.0001f) {
                        voice.active = false;
                        continue;
                    }
                }
                const float v = renderVoice(voice, lfo);
                sum += v * voice.amp;
            }
            outL[i] = sum;
            outR[i] = sum;
        }
        lfoPhase_ = lfoPhase_;
    }

protected:
    virtual void onNoteOn(Op1Voice &) {}
    virtual void onNoteOff(Op1Voice &) {}
    virtual float renderVoice(Op1Voice &voice, float lfo) = 0;

    int findFreeVoice() const {
        for (size_t i = 0; i < voices_.size(); ++i) {
            if (!voices_[i].active) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int sampleRate_ = 48000;
    int voiceCursor_ = 0;
    float lfoPhase_ = 0.0f;
    Op1Params params_{};
    std::vector<Op1Voice> voices_;
};

class ClusterEngine final : public Op1EngineBase {
protected:
    void onNoteOn(Op1Voice &voice) override {
        const float spread = 0.5f + params_.osc1Detune * 12.0f;
        const float detune = params_.osc2Detune * 4.0f;
        const float dur = 0.01f + params_.decay * 0.1f;
        for (auto &grain : voice.grains) {
            voice.noise = 1664525u * voice.noise + 1013904223u;
            const float r = (static_cast<int>(voice.noise >> 8) & 0xFFFF) / 32768.0f - 1.0f;
            const float semis = r * (spread + detune);
            grain.freq = voice.baseFreq * std::pow(2.0f, semis / 12.0f);
            grain.phase = r * kTwoPi;
            grain.age = 0.0f;
            grain.dur = dur * (0.6f + 0.8f * std::fabs(r));
            grain.amp = 0.4f + 0.6f * clamp01(params_.fmAmount + 0.5f * r);
        }
    }

    float renderVoice(Op1Voice &voice, float lfo) override {
        float sum = 0.0f;
        const float spread = 0.5f + params_.osc1Detune * 12.0f;
        const float detune = params_.osc2Detune * 4.0f;
        const float durBase = 0.01f + params_.decay * 0.1f;
        const float dt = 1.0f / static_cast<float>(sampleRate_);
        const float motion = params_.fmAmount;
        for (auto &grain : voice.grains) {
            if (grain.age >= grain.dur) {
                voice.noise = 1664525u * voice.noise + 1013904223u;
                const float r = (static_cast<int>(voice.noise >> 8) & 0xFFFF) / 32768.0f - 1.0f;
                const float semis = r * (spread + detune);
                grain.freq = voice.baseFreq * std::pow(2.0f, semis / 12.0f);
                grain.phase = r * kTwoPi;
                grain.age = 0.0f;
                grain.dur = durBase * (0.6f + 0.8f * std::fabs(r));
                grain.amp = 0.4f + 0.6f * clamp01(params_.fmAmount + 0.5f * r);
            }
            const float phaseNorm = grain.age / std::max(0.0001f, grain.dur);
            const float window = 0.5f - 0.5f * std::cos(kTwoPi * phaseNorm);
            sum += oscWave(params_.osc1Wave, grain.phase, voice.noise) * window * grain.amp;
            const float jitter = 1.0f + motion * 0.02f *
                                              (static_cast<float>((voice.noise >> 3) & 0xFF) /
                                               255.0f - 0.5f);
            grain.phase += kTwoPi * grain.freq * dt * jitter;
            grain.age += dt;
        }
        return sum * 0.35f;
    }
};

class DigitalEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const float detune = params_.osc1Detune * 0.6f;
        const float f1 = voice.baseFreq * (1.0f - detune * 0.5f);
        const float f2 = voice.baseFreq * (1.0f + detune * 0.5f);
        voice.phase1 += kTwoPi * f1 / sampleRate_;
        voice.phase2 += kTwoPi * f2 / sampleRate_;
        const int waveA = params_.osc1Wave;
        const int waveB = (waveA + 1) % 10;
        const float index = clamp01(params_.fmAmount);
        const float wave1 = oscWave(waveA, voice.phase1, voice.noise);
        const float wave2 = oscWave(waveB, voice.phase2, voice.noise);
        float mix = wave1 + (wave2 - wave1) * index;
        const float crush = 4.0f + params_.osc2Detune * 12.0f;
        const float step = 1.0f / crush;
        mix = std::round(mix / step) * step;
        const float drive = 1.0f + params_.feedback * 4.0f;
        return std::tanh(mix * drive);
    }
};

class DnaEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        voice.noise = 1664525u * voice.noise + 1013904223u;
        const float n = (static_cast<int>(voice.noise >> 8) & 0xFFFF) / 32768.0f - 1.0f;
        const float chaos = n * (0.2f + params_.feedback * 0.8f);
        const float freq = voice.baseFreq * (1.0f + chaos * 0.4f);
        voice.phase1 += kTwoPi * freq / sampleRate_;
        const float geneA = oscWave(params_.osc1Wave, voice.phase1, voice.noise);
        const float geneB = oscWave(params_.osc2Wave, voice.phase1 + chaos, voice.noise);
        const float mixAmt = clamp01(params_.fmAmount);
        const float base = geneA + (geneB - geneA) * mixAmt;
        float mix = base * (1.0f - params_.osc2Gain) + n * params_.osc2Gain;
        mix = std::tanh(mix * (1.0f + params_.feedback * 2.5f));
        return mix * 0.8f;
    }
};

class DrWaveEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const float bend = clamp01(params_.fmAmount);
        const int wave = params_.osc1Wave;
        const float det = params_.osc1Detune * 0.4f;
        const float freq = voice.baseFreq * (1.0f + det);
        voice.phase1 += kTwoPi * freq / sampleRate_;
        float base = oscWave(wave, voice.phase1, voice.noise);
        const float shaped = std::tanh(base * (1.0f + bend * 4.0f));
        const float drive = 1.0f + params_.feedback * 2.0f;
        return std::tanh(shaped * drive);
    }
};

class DSynthEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const float det1 = params_.osc1Detune * 0.4f;
        const float det2 = params_.osc2Detune * 0.4f;
        const float f1 = voice.baseFreq * std::pow(2.0f, det1 / 12.0f);
        const float f2 = voice.baseFreq * std::pow(2.0f, det2 / 12.0f);
        voice.phase1 += kTwoPi * f1 / sampleRate_;
        voice.phase2 += kTwoPi * f2 / sampleRate_;
        const float o1 = oscWave(params_.osc1Wave, voice.phase1, voice.noise) * params_.osc1Gain;
        const float o2 = oscWave(params_.osc2Wave, voice.phase2, voice.noise) * params_.osc2Gain;
        return std::tanh((o1 + o2) * 1.2f);
    }
};

class FmEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const float carrierRatio = 0.5f + params_.osc1Detune * 3.5f;
        const float f1 = voice.baseFreq * carrierRatio;
        const float f2 = voice.baseFreq * params_.ratio;
        voice.phase2 += kTwoPi * f2 / sampleRate_;
        const float mod = std::sin(voice.phase2 + params_.feedback * voice.last);
        voice.last = mod;
        voice.phase1 += kTwoPi * f1 / sampleRate_;
        return std::sin(voice.phase1 + mod * params_.fmAmount * 2.5f);
    }
};

class PulseEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const float dutyBase = 0.1f + params_.fmAmount * 0.8f;
        const float pwmRate = 0.5f + params_.feedback * 3.0f;
        voice.phase3 += kTwoPi * pwmRate / sampleRate_;
        if (voice.phase3 > kTwoPi) {
            voice.phase3 -= kTwoPi;
        }
        const float pwm = std::sin(voice.phase3) * params_.feedback * 0.25f;
        const float duty = clampRange(dutyBase + pwm, 0.05f, 0.95f);
        voice.phase1 += kTwoPi * voice.baseFreq / sampleRate_;
        const float t = std::fmod(voice.phase1, kTwoPi) / kTwoPi;
        float sample = (t < duty) ? 1.0f : -1.0f;
        const float sub = std::sin(voice.phase1 * 0.5f) * params_.osc2Gain;
        return sample * (1.0f - params_.osc2Gain) + sub;
    }
};

class PhaseEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const float f1 = voice.baseFreq;
        const float f2 = voice.baseFreq * params_.ratio;
        voice.phase2 += kTwoPi * f2 / sampleRate_;
        const float mod = std::sin(voice.phase2) * params_.ratio * (0.3f + params_.feedback * 1.7f);
        voice.phase1 += kTwoPi * f1 / sampleRate_;
        const float offset = params_.fmAmount * kTwoPi;
        const float spread = params_.osc1Detune * 0.5f;
        return std::sin(voice.phase1 + offset + mod + spread);
    }
};

class RingEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const float f1 = voice.baseFreq;
        const float f2 = voice.baseFreq * params_.ratio;
        voice.phase1 += kTwoPi * f1 / sampleRate_;
        voice.phase2 += kTwoPi * f2 / sampleRate_;
        const float a = std::sin(voice.phase1);
        const float b = std::sin(voice.phase2);
        const float ring = a * b;
        const float mix = a * (1.0f - params_.fmAmount) + ring * params_.fmAmount;
        const float drive = 1.0f + params_.feedback * 3.0f;
        return std::tanh(mix * drive);
    }
};

class StringEngine final : public Op1EngineBase {
protected:
    void onNoteOn(Op1Voice &voice) override {
        const float freq = std::max(40.0f, voice.baseFreq * (0.5f + params_.ratio * 0.5f));
        const int len = static_cast<int>(std::max(16.0f, sampleRate_ / freq));
        voice.delay.assign(static_cast<size_t>(len), 0.0f);
        for (auto &v : voice.delay) {
            voice.noise = 1664525u * voice.noise + 1013904223u;
            v = ((static_cast<int>(voice.noise >> 8) & 0xFFFF) / 32768.0f - 1.0f) *
                (0.5f + params_.osc2Gain * 0.5f);
        }
        voice.delayIdx = 0;
        voice.delayFilter = 0.0f;
        voice.amp = 1.0f;
    }

    float renderVoice(Op1Voice &voice, float) override {
        if (voice.delay.empty()) {
            return 0.0f;
        }
        const int len = static_cast<int>(voice.delay.size());
        const int idx = voice.delayIdx;
        const int idx2 = (idx + 1) % len;
        const float y = voice.delay[idx];
        float next = 0.5f * (y + voice.delay[idx2]);
        const float damp = 0.92f - params_.fmAmount * 0.4f;
        next *= damp;
        const float cutoff = 0.2f + params_.feedback * 0.6f;
        voice.delayFilter += (next - voice.delayFilter) * cutoff;
        voice.delay[idx] = voice.delayFilter;
        voice.delayIdx = (idx + 1) % len;
        voice.amp *= 0.9995f;
        if (!voice.keydown && voice.amp < 0.0003f) {
            voice.active = false;
        }
        return y * voice.amp;
    }
};

class VoltageEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float lfo) override {
        const int voices = std::max(1, std::min(8, params_.osc1Voices));
        const float det = params_.osc1Detune * 0.6f;
        float sum = 0.0f;
        const float baseInc = kTwoPi * voice.baseFreq / sampleRate_;
        const float basePhase = voice.phase1;
        const float mix = clamp01(params_.fmAmount);
        for (int i = 0; i < voices; ++i) {
            const float spread = (static_cast<float>(i) - (voices - 1) * 0.5f) /
                                 std::max(1.0f, (voices - 1) * 0.5f);
            const float detune = std::pow(2.0f, (spread * det) / 12.0f);
            const float phase = basePhase * detune + spread * 0.4f;
            const float saw = oscWave(1, phase, voice.noise);
            const float sq = oscWave(2, phase, voice.noise);
            sum += saw * (1.0f - mix) + sq * mix;
        }
        voice.phase1 += baseInc;
        const float drive = 1.0f + params_.feedback * 3.0f + params_.filterEnv * 2.0f;
        return std::tanh((sum / static_cast<float>(voices)) * params_.osc1Gain * drive);
    }
};

class SawEngine final : public Op1EngineBase {
protected:
    float renderVoice(Op1Voice &voice, float) override {
        const int voices = std::max(1, std::min(8, params_.osc1Voices));
        const float det = params_.osc1Detune * 0.8f;
        const float spread = params_.osc2Detune * 0.6f;
        float sum = 0.0f;
        const float base = voice.baseFreq;
        for (int i = 0; i < voices; ++i) {
            const float vSpread = (static_cast<float>(i) - (voices - 1) * 0.5f) /
                                  std::max(1.0f, (voices - 1) * 0.5f);
            const float freq = base * std::pow(2.0f, (vSpread * det) / 12.0f);
            voice.phase1 += kTwoPi * freq / sampleRate_;
            const float phase = voice.phase1 + vSpread * spread * kTwoPi * 0.25f;
            sum += oscWave(1, phase, voice.noise);
        }
        const float sub = std::sin(voice.phase1 * 0.5f) * params_.osc2Gain;
        voice.noise = 1664525u * voice.noise + 1013904223u;
        const float noise = ((static_cast<int>(voice.noise >> 8) & 0xFFFF) / 32768.0f - 1.0f) *
                            params_.feedback * 0.5f;
        return ((sum / static_cast<float>(voices)) + sub + noise) * 0.6f;
    }
};

} // namespace

std::unique_ptr<Op1Engine> createOp1Engine(Op1EngineType type) {
    switch (type) {
        case Op1EngineType::Cluster:
            return std::make_unique<ClusterEngine>();
        case Op1EngineType::Digital:
            return std::make_unique<DigitalEngine>();
        case Op1EngineType::DNA:
            return std::make_unique<DnaEngine>();
        case Op1EngineType::DrWave:
            return std::make_unique<DrWaveEngine>();
        case Op1EngineType::DSynth:
            return std::make_unique<DSynthEngine>();
        case Op1EngineType::FM:
            return std::make_unique<FmEngine>();
        case Op1EngineType::Pulse:
            return std::make_unique<PulseEngine>();
        case Op1EngineType::Phase:
            return std::make_unique<PhaseEngine>();
        case Op1EngineType::Ring:
            return std::make_unique<RingEngine>();
        case Op1EngineType::String:
            return std::make_unique<StringEngine>();
        case Op1EngineType::Saw:
            return std::make_unique<SawEngine>();
        case Op1EngineType::Voltage:
            return std::make_unique<VoltageEngine>();
        default:
            return std::make_unique<DigitalEngine>();
    }
}
