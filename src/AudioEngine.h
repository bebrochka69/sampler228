#pragma once

#include <QObject>
#include <QVector>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class AudioEngine : public QObject {
    Q_OBJECT
public:
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

    void setBusEffects(int bus, const std::vector<EffectSettings> &effects);
    float busMeter(int bus) const;
    void setBusGain(int bus, float gain);

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

    void start();
    void stop();
    void run();
    void mix(float *out, int frames);
    void processBus(int busIndex, float *buffer, int frames, float sidechainEnv);
    float computeEnv(const float *buffer, int frames) const;
    float computePeak(const float *buffer, int frames) const;

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
    void *m_pcmHandle = nullptr;

    std::array<std::atomic<float>, 8> m_padAttack{};
    std::array<std::atomic<float>, 8> m_padDecay{};
    std::array<std::atomic<float>, 8> m_padSustain{};
    std::array<std::atomic<float>, 8> m_padRelease{};
    std::vector<float> m_lastOut;
    bool m_lastOutValid = false;
};
