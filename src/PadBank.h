#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <array>
#include <memory>

class AudioEngine;

class PadBank : public QObject {
    Q_OBJECT
public:
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
        float start = 0.0f;
        float end = 1.0f;
        int sliceCountIndex = 0;
        int sliceIndex = 0;
        bool loop = false;
        int fxBus = 0;
        bool normalize = false;
    };

    struct SynthParams {
        float attack = 0.15f;
        float decay = 0.25f;
        float sustain = 0.7f;
        float release = 0.25f;
        int wave = 0;
        int voices = 1;
        float detune = 0.12f;
        int octave = 0;
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
    int fxBus(int index) const;
    void setFxBus(int index, int bus);

    bool isPlaying(int index) const;
    bool isPadReady(int index) const;
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
    static QStringList synthPresets();
    static QStringList serumWaves();
    static QStringList synthTypes();
    static bool hasFluidSynth();
    static bool hasZyn();
    void setBusEffects(int bus, const QVector<BusEffect> &effects);
    float busMeter(int bus) const;
    float busGain(int bus) const;
    void setBusGain(int bus, float gain);
    float normalizeGainForPad(int index) const;

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
};
