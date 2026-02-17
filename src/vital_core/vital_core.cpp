#include "vital_core.h"

#include <algorithm>
#include <cmath>
#include <vector>

#if defined(GROOVEBOX_WITH_VITAL)
#include "JuceHeader.h"
#include "sound_engine.h"
#include "synth_base.h"
#include "synth_constants.h"
#include "synth_parameters.h"
#include "wavetable_creator.h"

namespace {
constexpr float kEnvMaxSeconds = 2.37842f;

float clamp01(float v) {
    return std::max(0.0f, std::min(1.0f, v));
}

int waveToFrameIndex(int waveIndex) {
    const int shapes = vital::PredefinedWaveFrames::kNumShapes;
    const int safeWave = std::max(0, waveIndex);
    const int shape = (shapes > 0) ? (safeWave % shapes) : 0;
    return (vital::kNumOscillatorWaveFrames * shape) / std::max(1, shapes);
}

int filterModelForType(int type) {
    switch (type) {
        case 1:
            return vital::constants::kDigital;
        case 2:
            return vital::constants::kComb;
        case 3:
            return vital::constants::kPhase;
        default:
            return vital::constants::kAnalog;
    }
}

class VitalHeadless : public HeadlessSynth {
public:
    void renderToBuffer(AudioSampleBuffer &buffer, int samples) {
        processAudio(&buffer, buffer.getNumChannels(), samples, 0);
    }
};
} // namespace
#endif

struct VitalCore::Impl {
#if defined(GROOVEBOX_WITH_VITAL)
    std::unique_ptr<juce::ScopedJuceInitialiser_GUI> juceInit;
    VitalHeadless synth;
    AudioSampleBuffer buffer;
    Params params;
    int sampleRate = 48000;
    int voices = 8;
    bool initialized = false;
    int osc1Wave = -1;
    int osc2Wave = -1;

    void ensureInit() {
        if (initialized) {
            return;
        }
        if (!juceInit) {
            juceInit = std::make_unique<juce::ScopedJuceInitialiser_GUI>();
        }
        synth.getEngine()->setSampleRate(sampleRate);
        synth.getEngine()->setBpm(120.0f);
        synth.loadInitPreset();
        if (auto *creator = synth.getWavetableCreator(0)) {
            creator->initPredefinedWaves();
        }
        if (auto *creator = synth.getWavetableCreator(1)) {
            creator->initPredefinedWaves();
        }
        setControl("osc_3_on", 0.0f);
        setControl("sample_on", 0.0f);
        setControl("filter_2_on", 0.0f);

        // Disable built-in FX by default.
        const char *fxOff[] = {"chorus_on", "delay_on", "reverb_on", "distortion_on",
                               "phaser_on", "compressor_on"};
        for (const char *name : fxOff) {
            if (vital::Parameters::isParameter(name)) {
                synth.valueChanged(name, 0.0f);
            }
        }
        setControl("oversampling", 0.0f);

        initialized = true;
    }

    void setControl(const std::string &name, float value) {
        if (!vital::Parameters::isParameter(name)) {
            return;
        }
        synth.valueChanged(name, value);
    }

    void applyWave(int oscIndex, int waveIndex) {
        const int frame = waveToFrameIndex(waveIndex);
        const std::string param = "osc_" + std::to_string(oscIndex) + "_wave_frame";
        setControl(param, static_cast<float>(frame));
    }

    void applyParams(float bpm) {
        ensureInit();

        setControl("beats_per_minute", bpm);
        setControl("polyphony", static_cast<float>(voices));

        setControl("osc_1_on", 1.0f);
        setControl("osc_2_on", 1.0f);
        setControl("osc_1_unison_voices",
                   static_cast<float>(std::max(1, std::min(16, params.osc1Voices))));
        setControl("osc_2_unison_voices",
                   static_cast<float>(std::max(1, std::min(16, params.osc2Voices))));
        setControl("osc_1_unison_detune", clamp01(params.osc1Detune) * 10.0f);
        setControl("osc_2_unison_detune", clamp01(params.osc2Detune) * 10.0f);
        setControl("osc_1_stereo_spread", clamp01(params.osc1Detune));
        setControl("osc_2_stereo_spread", clamp01(params.osc2Detune));
        setControl("osc_1_level", clamp01(params.osc1Gain));
        setControl("osc_2_level", clamp01(params.osc2Gain));
        setControl("osc_1_pan", std::max(-1.0f, std::min(1.0f, params.osc1Pan)));
        setControl("osc_2_pan", std::max(-1.0f, std::min(1.0f, params.osc2Pan)));

        if (osc1Wave != params.osc1Wave) {
            osc1Wave = params.osc1Wave;
            applyWave(1, osc1Wave);
        }
        if (osc2Wave != params.osc2Wave) {
            osc2Wave = params.osc2Wave;
            applyWave(2, osc2Wave);
        }

        setControl("filter_1_on", 1.0f);
        const float cutoff = 8.0f + clamp01(params.cutoff) * 128.0f;
        setControl("filter_1_cutoff", cutoff);
        setControl("filter_1_resonance", clamp01(params.resonance));
        setControl("filter_1_model", static_cast<float>(filterModelForType(params.filterType)));
        setControl("filter_1_style", 0.0f);

        setControl("env_1_delay", 0.0f);
        setControl("env_1_hold", 0.0f);
        setControl("env_1_attack", clamp01(params.attack) * kEnvMaxSeconds);
        setControl("env_1_decay", clamp01(params.decay) * kEnvMaxSeconds);
        setControl("env_1_sustain", clamp01(params.sustain));
        setControl("env_1_release", clamp01(params.release) * kEnvMaxSeconds);
    }
#else
    void ensureInit() { }
    void applyParams(float) { }
#endif
};

VitalCore::VitalCore() : impl_(std::make_unique<Impl>()) {}
VitalCore::~VitalCore() = default;

void VitalCore::init(int sampleRate, int voices) {
#if defined(GROOVEBOX_WITH_VITAL)
    impl_->sampleRate = (sampleRate > 0) ? sampleRate : 48000;
    impl_->voices = std::max(1, std::min(16, voices));
    impl_->initialized = false;
    impl_->ensureInit();
#else
    (void)sampleRate;
    (void)voices;
#endif
}

void VitalCore::setParams(const Params &params, float bpm) {
#if defined(GROOVEBOX_WITH_VITAL)
    impl_->params = params;
    impl_->applyParams(bpm);
#else
    (void)params;
    (void)bpm;
#endif
}

void VitalCore::noteOn(int note, int velocity) {
#if defined(GROOVEBOX_WITH_VITAL)
    impl_->ensureInit();
    const float vel = std::max(0.0f, std::min(1.0f, static_cast<float>(velocity) / 127.0f));
    impl_->synth.getEngine()->noteOn(note, vel, 0, 0);
#else
    (void)note;
    (void)velocity;
#endif
}

void VitalCore::noteOff(int note) {
#if defined(GROOVEBOX_WITH_VITAL)
    impl_->ensureInit();
    impl_->synth.getEngine()->noteOff(note, 0.0f, 0, 0);
#else
    (void)note;
#endif
}

void VitalCore::render(float *outL, float *outR, int frames) {
#if defined(GROOVEBOX_WITH_VITAL)
    if (!outL || !outR || frames <= 0) {
        return;
    }
    impl_->ensureInit();
    const int maxBlock = vital::kMaxBufferSize;
    if (impl_->buffer.getNumSamples() < maxBlock || impl_->buffer.getNumChannels() < 2) {
        impl_->buffer.setSize(2, maxBlock, false, false, true);
    }
    int remaining = frames;
    int offset = 0;
    while (remaining > 0) {
        const int chunk = std::min(remaining, maxBlock);
        impl_->synth.renderToBuffer(impl_->buffer, chunk);
        const float *left = impl_->buffer.getReadPointer(0);
        const float *right = impl_->buffer.getReadPointer(1);
        std::copy(left, left + chunk, outL + offset);
        std::copy(right, right + chunk, outR + offset);
        offset += chunk;
        remaining -= chunk;
    }
#else
    if (!outL || !outR || frames <= 0) {
        return;
    }
    std::fill(outL, outL + frames, 0.0f);
    std::fill(outR, outR + frames, 0.0f);
#endif
}
