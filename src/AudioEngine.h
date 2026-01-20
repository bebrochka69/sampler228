#pragma once

#include <QObject>
#include <QVector>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class AudioEngine : public QObject {
    Q_OBJECT
public:
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
                 bool loop, float volume, float pan);
    void stopPad(int padId);
    void stopAll();
    bool isPadActive(int padId) const;

private:
    struct Voice {
        int padId = -1;
        std::shared_ptr<Buffer> buffer;
        int startFrame = 0;
        int endFrame = 0;
        int position = 0;
        bool loop = false;
        float gainL = 1.0f;
        float gainR = 1.0f;
    };

    void start();
    void stop();
    void run();
    void mix(float *out, int frames);

    bool m_available = false;
    int m_sampleRate = 48000;
    int m_channels = 2;
    int m_periodFrames = 256;

    std::atomic<bool> m_running{false};
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    void *m_pcmHandle = nullptr;
};
