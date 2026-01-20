#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#ifdef GROOVEBOX_WITH_ALSA
#include <alsa/asoundlib.h>
#endif

namespace {
float clampSample(float v) {
    if (v > 1.0f) {
        return 1.0f;
    }
    if (v < -1.0f) {
        return -1.0f;
    }
    return v;
}

void computePanGains(float pan, float volume, float &left, float &right) {
    pan = std::max(-1.0f, std::min(1.0f, pan));
    const float l = pan <= 0.0f ? 1.0f : 1.0f - pan;
    const float r = pan >= 0.0f ? 1.0f : 1.0f + pan;
    left = volume * l;
    right = volume * r;
}
}  // namespace

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {
#ifdef GROOVEBOX_WITH_ALSA
    start();
#endif
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::start() {
#ifdef GROOVEBOX_WITH_ALSA
    if (m_running) {
        return;
    }

    snd_pcm_t *pcm = nullptr;
    if (snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0) {
        m_available = false;
        return;
    }

    snd_pcm_hw_params_t *params = nullptr;
    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(pcm, params);
    snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm, params, static_cast<unsigned int>(m_channels));

    unsigned int rate = static_cast<unsigned int>(m_sampleRate);
    snd_pcm_hw_params_set_rate_near(pcm, params, &rate, nullptr);
    m_sampleRate = static_cast<int>(rate);

    snd_pcm_uframes_t period = static_cast<snd_pcm_uframes_t>(m_periodFrames);
    snd_pcm_hw_params_set_period_size_near(pcm, params, &period, nullptr);
    m_periodFrames = static_cast<int>(period);

    snd_pcm_uframes_t bufferSize = period * 4;
    snd_pcm_hw_params_set_buffer_size_near(pcm, params, &bufferSize);

    if (snd_pcm_hw_params(pcm, params) < 0) {
        snd_pcm_hw_params_free(params);
        snd_pcm_close(pcm);
        m_available = false;
        return;
    }

    snd_pcm_hw_params_free(params);
    snd_pcm_prepare(pcm);

    m_pcmHandle = pcm;
    m_available = true;
    m_running = true;
    m_thread = std::thread(&AudioEngine::run, this);
#endif
}

void AudioEngine::stop() {
    if (!m_running) {
        return;
    }

    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }

#ifdef GROOVEBOX_WITH_ALSA
    if (m_pcmHandle) {
        snd_pcm_t *pcm = static_cast<snd_pcm_t *>(m_pcmHandle);
        snd_pcm_drop(pcm);
        snd_pcm_close(pcm);
        m_pcmHandle = nullptr;
    }
#endif
    m_available = false;
}

void AudioEngine::trigger(int padId, const std::shared_ptr<Buffer> &buffer, int startFrame,
                          int endFrame, bool loop, float volume, float pan) {
    if (!m_available || !buffer || !buffer->isValid()) {
        return;
    }

    if (startFrame < 0) {
        startFrame = 0;
    }
    const int totalFrames = buffer->frames();
    if (endFrame <= 0 || endFrame > totalFrames) {
        endFrame = totalFrames;
    }
    if (startFrame >= endFrame) {
        return;
    }

    float gainL = 1.0f;
    float gainR = 1.0f;
    computePanGains(pan, volume, gainL, gainR);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                  [padId](const Voice &voice) { return voice.padId == padId; }),
                   m_voices.end());

    Voice voice;
    voice.padId = padId;
    voice.buffer = buffer;
    voice.startFrame = startFrame;
    voice.endFrame = endFrame;
    voice.position = startFrame;
    voice.loop = loop;
    voice.gainL = gainL;
    voice.gainR = gainR;
    m_voices.push_back(std::move(voice));
}

void AudioEngine::stopPad(int padId) {
    if (!m_available) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                  [padId](const Voice &voice) { return voice.padId == padId; }),
                   m_voices.end());
}

void AudioEngine::stopAll() {
    if (!m_available) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.clear();
}

bool AudioEngine::isPadActive(int padId) const {
    if (!m_available) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const Voice &voice : m_voices) {
        if (voice.padId == padId) {
            return true;
        }
    }
    return false;
}

void AudioEngine::run() {
#ifdef GROOVEBOX_WITH_ALSA
    snd_pcm_t *pcm = static_cast<snd_pcm_t *>(m_pcmHandle);
    if (!pcm) {
        return;
    }

    const int framesPerPeriod = std::max(1, m_periodFrames);
    std::vector<float> mixBuffer(framesPerPeriod * m_channels, 0.0f);
    std::vector<int16_t> out(framesPerPeriod * m_channels, 0);

    while (m_running) {
        std::fill(mixBuffer.begin(), mixBuffer.end(), 0.0f);
        mix(mixBuffer.data(), framesPerPeriod);

        for (int i = 0; i < framesPerPeriod * m_channels; ++i) {
            const float v = clampSample(mixBuffer[i]);
            out[i] = static_cast<int16_t>(v * 32767.0f);
        }

        int framesLeft = framesPerPeriod;
        int offset = 0;
        while (framesLeft > 0 && m_running) {
            const snd_pcm_sframes_t written = snd_pcm_writei(
                pcm, out.data() + offset * m_channels, framesLeft);
            if (written < 0) {
                const int err = snd_pcm_recover(pcm, static_cast<int>(written), 1);
                if (err < 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            } else {
                framesLeft -= static_cast<int>(written);
                offset += static_cast<int>(written);
            }
        }
    }
#endif
}

void AudioEngine::mix(float *out, int frames) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_voices.begin(); it != m_voices.end();) {
        Voice &voice = *it;
        if (!voice.buffer || !voice.buffer->isValid()) {
            it = m_voices.erase(it);
            continue;
        }

        const float *data = voice.buffer->samples.constData();
        const int channels = voice.buffer->channels;
        const int totalFrames = voice.buffer->frames();

        int pos = voice.position;
        bool done = false;
        for (int i = 0; i < frames; ++i) {
            if (pos >= voice.endFrame) {
                if (voice.loop) {
                    pos = voice.startFrame;
                } else {
                    done = true;
                    break;
                }
            }
            if (pos >= totalFrames) {
                done = true;
                break;
            }

            const int idx = pos * channels;
            float left = data[idx];
            float right = (channels > 1 && idx + 1 < voice.buffer->samples.size()) ? data[idx + 1]
                                                                                   : left;
            out[i * m_channels] += left * voice.gainL;
            out[i * m_channels + 1] += right * voice.gainR;
            ++pos;
        }

        voice.position = pos;
        if (done) {
            it = m_voices.erase(it);
        } else {
            ++it;
        }
    }
}
