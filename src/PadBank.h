#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <array>
#include <memory>

#include "AudioEngine.h"

class QTimer;
class QProcess;

class PadBank : public QObject {
    Q_OBJECT
public:
    static constexpr int kLfoModuleCount = 4;
    static constexpr int kEnvModuleCount = 4;
    static constexpr int kFilterModuleCount = 4;
    static constexpr int kLfoPatternSteps = 32;

    enum class ModTarget {
        None = 0,
        Osc1Detune,
        Osc1Gain,
        Osc1Pan,
        Osc2Detune,
        Osc2Gain,
        Osc2Pan,
        Cutoff,
        Resonance,
        FilterEnv,
        FmAmount,
        Ratio,
        Feedback,
        Custom1,
        Custom2,
        Custom3,
        Custom4,
        Count
    };
    static constexpr int kModTargetCount = static_cast<int>(ModTarget::Count);

    explicit PadBank(QObject *parent = nullptr);
    ~PadBank() override;

    int padCount() const { return static_cast<int>(m_paths.size()); }
    int activePad() const { return m_activePad; }
    void setActivePad(int index);

    void setPadPath(int index, const QString &path);
    void copyPad(int from, int to);
    QString padPath(int index) const;
    QString padName(int index) const;
    bool isLoaded(int index) const;
    bool isSynth(int index) const;
    QString synthName(int index) const;
    QString synthId(int index) const;
    void setSynth(int index, const QString &name);

    struct PadParams {
        float volume = 1.0f;
        float pan = 0.0f;
        float pitch = 0.0f;
        int stretchIndex = 0;
        int stretchMode = 0;
        float start = 0.0f;
        float end = 1.0f;
        int sliceCountIndex = 0;
        int sliceIndex = 0;
        bool loop = false;
        int fxBus = 0;
        bool normalize = false;
    };

    struct SynthParams {
        struct LfoModule {
            bool enabled = false;
            int kind = 0;  // 0=smooth, 1=trance
            int shape = 0;
            float morph = 0.0f;
            float rate = 0.2f;
            float depth = 0.5f;
            int sync = 1;
            int syncIndex = 3;
            int steps = 16;
            std::array<float, kLfoPatternSteps> pattern{};
            std::array<float, kModTargetCount> assign{};
        };

        struct EnvModule {
            bool enabled = false;
            float attack = 0.0f;
            float decay = 0.25f;
            float sustain = 1.0f;
            float release = 0.25f;
            std::array<float, kModTargetCount> assign{};
        };

        struct FilterModule {
            bool enabled = false;
            int preset = 0;
            int type = 0;
            float lowCut = 0.0f;
            float highCut = 1.0f;
            float resonance = 0.0f;
            float slope = 0.0f;
            float drive = 0.0f;
            float mix = 1.0f;
        };

        float attack = 0.15f;
        float decay = 0.25f;
        float sustain = 0.7f;
        float release = 0.25f;
        int wave = 1;
        int voices = 8;
        float detune = 0.12f;
        int octave = 0;
        float fmAmount = 0.4f;
        float ratio = 1.0f;
        float feedback = 0.0f;
        float cutoff = 0.8f;
        float resonance = 0.1f;
        float filterEnv = 0.0f;
        int filterType = 0;
        float lfoRate = 0.2f;
        float lfoDepth = 0.0f;
        int lfoShape = 0;
        int lfoSync = 0;
        int lfoSyncIndex = 0;
        int lfoTarget = 0;
        int osc1Wave = 1;
        int osc2Wave = 1;
        int osc1Voices = 1;
        int osc2Voices = 1;
        float osc1Detune = 0.0f;
        float osc2Detune = 0.0f;
        float osc1Gain = 0.8f;
        float osc2Gain = 0.6f;
        float osc1Pan = -0.1f;
        float osc2Pan = 0.1f;
        std::array<float, 8> macros{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
        std::array<float, kModTargetCount> lfoAssign{};
        std::array<float, kModTargetCount> envAssign{};
        std::array<LfoModule, kLfoModuleCount> lfoModules{};
        std::array<EnvModule, kEnvModuleCount> envModules{};
        std::array<FilterModule, kFilterModuleCount> filterModules{};
    };

    struct BusEffect {
        int type = 0;
        float p1 = 0.5f;
        float p2 = 0.5f;
        float p3 = 0.5f;
        float p4 = 0.5f;
        float p5 = 0.0f;
    };

    PadParams params(int index) const;
    SynthParams synthParams(int index) const;
    void setVolume(int index, float value);
    void setPan(int index, float value);
    void setPitch(int index, float semitones);
    void setStretchIndex(int index, int stretchIndex);
    void setStretchMode(int index, int mode);
    void setStart(int index, float value);
    void setEnd(int index, float value);
    void setSliceCountIndex(int index, int sliceCountIndex);
    void setSliceIndex(int index, int sliceIndex);
    void setLoop(int index, bool loop);
    void setNormalize(int index, bool enabled);
    void setSynthAdsr(int index, float attack, float decay, float sustain, float release);
    void setSynthWave(int index, int wave);
    void setSynthVoices(int index, int voices);
    void setSynthDetune(int index, float detune);
    void setSynthOctave(int index, int octave);
    void setSynthFm(int index, float fmAmount, float ratio, float feedback);
    void setSynthFilter(int index, float cutoff, float resonance);
    void setSynthFilterEnv(int index, float amount);
    void setSynthFilterType(int index, int type);
    void setSynthOsc(int index, int osc, int wave, int voices, float detune, float gain,
                     float pan);
    void setSynthLfo(int index, float rate, float depth);
    void setSynthLfoShape(int index, int shape);
    void setSynthLfoSync(int index, int enabled, int syncIndex);
    void setSynthLfoTarget(int index, int target);
    void setSynthModAssign(int index, ModTarget target, float lfoAmount, float envAmount);
    void setSynthLfoModule(int index, int slot, const SynthParams::LfoModule &module);
    void setSynthEnvModule(int index, int slot, const SynthParams::EnvModule &module);
    void setSynthFilterModule(int index, int slot, const SynthParams::FilterModule &module);
    void setSynthMacro(int index, int macro, float value);
    int fxBus(int index) const;
    void setFxBus(int index, int bus);

    bool isPlaying(int index) const;
    bool isPadReady(int index) const;
    float padPlayhead(int index) const;
    std::shared_ptr<AudioEngine::Buffer> rawBuffer(int index) const;
    void requestRawBuffer(int index);
    void triggerPad(int index);
    void triggerPadMidi(int index, int midiNote, int lengthSteps);
    void stopPad(int index);
    void stopAll();

    int bpm() const { return m_bpm; }
    void setBpm(int bpm);

    static int stretchCount();
    static QString stretchLabel(int index);
    static int sliceCountForIndex(int index);
    static QString fxBusLabel(int index);
    static QStringList synthBanks();
    static QStringList synthPresets();
    static QStringList synthPresetsForBank(const QString &bank);
    static QStringList serumWaves();
    static QStringList synthTypes();
    static bool hasMiniDexed();
    int synthVoiceParam(int index, int param) const;
    void setSynthVoiceParam(int index, int param, int value);
    void setBusEffects(int bus, const QVector<BusEffect> &effects);
    float busMeter(int bus) const;
    float busGain(int bus) const;
    void setBusGain(int bus, float gain);
    bool setAudioDevice(const QString &device);
    QString audioDevice() const;
    int engineSampleRate() const { return m_engineRate; }
    bool startRecording(const QString &path, int durationMs, int targetRate);
    void triggerMetronome(bool accent);
    float normalizeGainForPad(int index) const;
    bool previewSample(const QString &path, int *durationMs = nullptr);
    void stopPreview();
    bool isPreviewActive() const { return m_previewActive; }

signals:
    void padChanged(int index);
    void activePadChanged(int index);
    void padParamsChanged(int index);
    void bpmChanged(int bpm);

private:
    struct PadRuntime;
    static void rebuildSynthRuntime(PadRuntime *rt, const QString &name, int sampleRate,
                                    int baseMidi, const SynthParams &params);
    void scheduleRawRender(int index);
    void scheduleProcessedRender(int index);
    bool needsProcessing(const PadParams &params) const;

    std::array<QString, 8> m_paths;
    std::array<bool, 8> m_isSynth{};
    std::array<QString, 8> m_synthNames;
    std::array<QString, 8> m_synthBanks;
    std::array<int, 8> m_synthPrograms{};
    std::array<int, 8> m_synthBaseMidi{};
    std::array<SynthParams, 8> m_synthParams;
    std::array<PadParams, 8> m_params;
    std::array<PadRuntime *, 8> m_runtime;
    int m_activePad = 0;
    int m_bpm = 120;
    int m_engineRate = 48000;
    bool m_engineAvailable = false;
    int m_renderSerial = 0;
    QString m_ffmpegPath;
    std::unique_ptr<class AudioEngine> m_engine;
    std::array<float, 6> m_busGain{};
    QTimer *m_synthConnectTimer = nullptr;
    std::shared_ptr<AudioEngine::Buffer> m_metronomeBuffer;
    std::shared_ptr<AudioEngine::Buffer> m_metronomeAccent;
    QProcess *m_previewProcess = nullptr;
    QByteArray m_previewBytes;
    std::shared_ptr<AudioEngine::Buffer> m_previewBuffer;
    QString m_previewPath;
    int m_previewSampleRate = 0;
    bool m_previewActive = false;
};
