#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "dx7_core.h"
#include "vital_core/vital_core.h"

class AudioEngine : public QObject {
    Q_OBJECT
public:
    enum class SynthKind {
        Dx7,
        Vital
    };

    struct FmParams {
        float fmAmount = 0.4f;
        float ratio = 1.0f;
        float feedback = 0.0f;
        float cutoff = 0.8f;
        float resonance = 0.1f;
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
        float osc1Pan = -0.1f;
        float osc2Pan = 0.1f;
        float attack = 0.15f;
        float decay = 0.25f;
        float sustain = 0.7f;
        float release = 0.25f;
        std::array<float, 8> macros{};
    };
    struct EffectSettings {
        int type = 0;
        float p1 = 0.5f;
        float p2 = 0.5f;
        float p3 = 0.5f;
        float p4 = 0.5f;
        float p5 = 0.0f;
    };

    struct Buffer {
        QVector<float> samples;
        int channels = 2;
        int sampleRate = 0;

        int frames() const {
            return channels > 0 ? samples.size() / channels : 0;
        }

        bool isValid() const {
            return sampleRate > 0 && !samples.isEmpty();
        }
    };

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool isAvailable() const { return m_available; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }

    void trigger(int padId, const std::shared_ptr<Buffer> &buffer, int startFrame, int endFrame,
                 bool loop, float volume, float pan, float rate, int bus);
    void stopPad(int padId);
    void stopAll();
    bool isPadActive(int padId) const;

    void setPadAdsr(int padId, float attack, float decay, float sustain, float release);

    void setSynthEnabled(int padId, bool enabled);
    void setSynthKind(int padId, SynthKind kind);
    void setSynthParams(int padId, float volume, float pan, int bus);
    void setSynthVoices(int padId, int voices);
    void setFmParams(int padId, const FmParams &params);
    void synthNoteOn(int padId, int midiNote, int velocity);
    void synthNoteOff(int padId, int midiNote);
    void synthAllNotesOff(int padId);
    bool isSynthActive(int padId) const;
    bool loadSynthSysex(int padId, const QString &path);
    bool setSynthProgram(int padId, int program);
    int synthProgramCount(int padId) const;
    QString synthProgramName(int padId, int index) const;
    int synthVoiceParam(int padId, int param) const;
    bool setSynthVoiceParam(int padId, int param, int value);

    void setBusEffects(int bus, const std::vector<EffectSettings> &effects);
    float busMeter(int bus) const;
    void setBusGain(int bus, float gain);
    void setBpm(int bpm);
    bool startRecording(const QString &path, int totalFrames, int targetSampleRate);
    bool isRecording() const { return m_recording.load(); }
    float padPlayhead(int padId) const;

private:
    enum class EnvStage {
        Attack,
        Decay,
        Sustain,
        Release
    };

    struct Voice {
        int padId = -1;
        int bus = 0;
        std::shared_ptr<Buffer> buffer;
        int startFrame = 0;
        int endFrame = 0;
        double position = 0.0;
        bool loop = false;
        float gainL = 1.0f;
        float gainR = 1.0f;
        float rate = 1.0f;
        float env = 0.0f;
        EnvStage envStage = EnvStage::Attack;
        bool releaseRequested = false;
        bool useEnv = true;
    };

    struct EffectState {
        int type = 0;
        float p1 = 0.0f;
        float p2 = 0.0f;
        float p3 = 0.0f;
        float p4 = 0.0f;
        float p5 = 0.0f;
        std::vector<float> bufA;
        std::vector<float> bufB;
        int indexA = 0;
        int indexB = 0;
        float phase = 0.0f;
        float env = 0.0f;
        float z1L = 0.0f;
        float z1R = 0.0f;
        float readPosA = 0.0f;
        float readPosB = 0.0f;
        float readPosC = 0.0f;
        float readPosD = 0.0f;
        float phaseA = 0.0f;
        float phaseB = 0.5f;
        float phaseC = 0.0f;
        float phaseD = 0.5f;
        int grainSize = 0;
        float eqLp1L = 0.0f;
        float eqLp1R = 0.0f;
        float eqHp1L = 0.0f;
        float eqHp1R = 0.0f;
        float eqLp2L = 0.0f;
        float eqLp2R = 0.0f;
        float eqHp2L = 0.0f;
        float eqHp2R = 0.0f;
        float eqLowL = 0.0f;
        float eqLowR = 0.0f;
        float eqHighL = 0.0f;
        float eqHighR = 0.0f;
    };

    struct BusChain {
        std::vector<EffectState> effects;
    };

    struct SynthState {
        Dx7Core core;
        VitalCore vital;
        SynthKind kind = SynthKind::Dx7;
        bool enabled = false;
        bool initialized = false;
        int bus = 0;
        float gainL = 1.0f;
        float gainR = 1.0f;
        int voices = 8;
        std::array<bool, 128> activeNotes{};
        QString bankPath;
        bool bankLoaded = false;
        int programIndex = 0;
        FmParams fmParams;
        float filterCutoff = 0.8f;
        float filterResonance = 0.1f;
        int filterType = 0;
        float lfoRate = 0.2f;
        float lfoDepth = 0.0f;
        float lfoPhase = 0.0f;
        float filterIc1L = 0.0f;
        float filterIc2L = 0.0f;
        float filterIc1R = 0.0f;
        float filterIc2R = 0.0f;
        float env = 0.0f;
        EnvStage envStage = EnvStage::Attack;
        bool releaseRequested = false;
    };

    void start();
    void stop();
    void run();
    void mix(float *out, int frames);
    void processBus(int busIndex, float *buffer, int frames, float sidechainEnv);
    float computeEnv(const float *buffer, int frames) const;
    float computePeak(const float *buffer, int frames) const;
    void ensureSynthInit(SynthState &state);

    bool m_available = false;
    int m_sampleRate = 48000;
    int m_channels = 2;
    int m_periodFrames = 256;

    std::atomic<bool> m_running{false};
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    std::array<BusChain, 6> m_busChains;
    std::array<std::vector<float>, 6> m_busBuffers;
    std::array<std::atomic<float>, 6> m_busMeters{};
    std::array<std::atomic<float>, 6> m_busGains{};
    std::atomic<bool> m_hasSidechain{false};
    std::atomic<float> m_bpm{120.0f};

    std::atomic<bool> m_recording{false};
    int m_recordFramesLeft = 0;
    int m_recordTargetRate = 0;
    QString m_recordPath;
    std::vector<float> m_recordFloat;
    std::mutex m_recordMutex;
    void *m_pcmHandle = nullptr;

    std::array<std::atomic<float>, 8> m_padAttack{};
    std::array<std::atomic<float>, 8> m_padDecay{};
    std::array<std::atomic<float>, 8> m_padSustain{};
    std::array<std::atomic<float>, 8> m_padRelease{};
    std::array<SynthState, 8> m_synthStates{};
    std::vector<float> m_synthScratchL;
    std::vector<float> m_synthScratchR;
    std::vector<float> m_lastOut;
    bool m_lastOutValid = false;
    std::array<std::atomic<float>, 8> m_padPlayheads{};
};
