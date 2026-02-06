#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QFile>

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

float safeParam(float v) {
    if (!std::isfinite(v)) {
        return 0.5f;
    }
    return std::max(0.0f, std::min(1.0f, v));
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
#ifndef GROOVEBOX_WITH_ALSA
    for (auto &gain : m_busGains) {
        gain.store(1.0f);
    }
#endif
#ifdef GROOVEBOX_WITH_ALSA
    for (auto &gain : m_busGains) {
        gain.store(1.0f);
    }
    start();
#endif

    for (size_t i = 0; i < m_padAttack.size(); ++i) {
        m_padAttack[i].store(0.0f);
        m_padDecay[i].store(0.0f);
        m_padSustain[i].store(1.0f);
        m_padRelease[i].store(0.0f);
    }
}

AudioEngine::~AudioEngine() {
    stop();
}

void AudioEngine::start() {
#ifdef GROOVEBOX_WITH_ALSA
    if (m_running) {
        return;
    }

    auto deviceList = []() {
        auto detectHeadphones = []() -> QString {
#ifdef Q_OS_LINUX
            QFile file("/proc/asound/cards");
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return QString();
            }
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                if (line.contains("Headphones", Qt::CaseInsensitive)) {
                    const QString index = line.section(' ', 0, 0).trimmed();
                    if (!index.isEmpty() && index[0].isDigit()) {
                        return index;
                    }
                }
            }
#endif
            return QString();
        };
        QStringList list;
        const QString envSingle = qEnvironmentVariable("GROOVEBOX_ALSA_DEVICE");
        const QString envList = qEnvironmentVariable("GROOVEBOX_ALSA_DEVICES");
        if (!envSingle.isEmpty()) {
            list << envSingle;
        }
        if (!envList.isEmpty()) {
            for (const QString &item : envList.split(',', Qt::SkipEmptyParts)) {
                const QString trimmed = item.trimmed();
                if (!trimmed.isEmpty()) {
                    list << trimmed;
                }
            }
        }
        const QString card = detectHeadphones();
        if (!card.isEmpty()) {
            list << QString("hw:%1,0").arg(card) << QString("plughw:%1,0").arg(card);
        }
        if (list.isEmpty()) {
            list << "default"
                 << "plughw:0,0"
                 << "hw:0,0"
                 << "sysdefault"
                 << "plughw:1,0"
                 << "hw:1,0";
        }
        list.removeDuplicates();
        return list;
    };

    snd_pcm_t *pcm = nullptr;
    const QStringList devices = deviceList();
    for (const QString &dev : devices) {
        if (snd_pcm_open(&pcm, dev.toLocal8Bit().constData(),
                         SND_PCM_STREAM_PLAYBACK, 0) >= 0) {
            break;
        }
        pcm = nullptr;
    }

    if (!pcm) {
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
                          int endFrame, bool loop, float volume, float pan, float rate, int bus) {
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

    rate = std::max(0.125f, std::min(4.0f, rate));
    float gainL = 1.0f;
    float gainR = 1.0f;
    computePanGains(pan, volume, gainL, gainR);

    std::lock_guard<std::mutex> lock(m_mutex);
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(),
                                  [padId](const Voice &voice) { return voice.padId == padId; }),
                   m_voices.end());

    Voice voice;
    voice.padId = padId;
    voice.bus = bus;
    voice.buffer = buffer;
    voice.startFrame = startFrame;
    voice.endFrame = endFrame;
    voice.position = static_cast<double>(startFrame);
    voice.loop = loop;
    voice.gainL = gainL;
    voice.gainR = gainR;
    voice.rate = rate;
    voice.env = 0.0f;
    voice.envStage = EnvStage::Attack;
    voice.releaseRequested = false;
    m_voices.push_back(std::move(voice));
}

void AudioEngine::stopPad(int padId) {
    if (!m_available) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    for (Voice &voice : m_voices) {
        if (voice.padId == padId) {
            voice.releaseRequested = true;
            voice.loop = false;
        }
    }
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

void AudioEngine::setBusEffects(int bus, const std::vector<EffectSettings> &effects) {
    if (bus < 0 || bus >= static_cast<int>(m_busChains.size())) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    BusChain &chain = m_busChains[static_cast<size_t>(bus)];
    chain.effects.clear();
    for (const EffectSettings &cfg : effects) {
        if (cfg.type <= 0) {
            continue;
        }
        EffectState state;
        state.type = cfg.type;
        state.p1 = cfg.p1;
        state.p2 = cfg.p2;
        state.p3 = cfg.p3;
        state.p4 = cfg.p4;
        state.p5 = cfg.p5;
        chain.effects.push_back(std::move(state));
    }
}

float AudioEngine::busMeter(int bus) const {
    if (bus < 0 || bus >= static_cast<int>(m_busMeters.size())) {
        return 0.0f;
    }
    return m_busMeters[static_cast<size_t>(bus)].load();
}

void AudioEngine::setBusGain(int bus, float gain) {
    if (bus < 0 || bus >= static_cast<int>(m_busGains.size())) {
        return;
    }
    m_busGains[static_cast<size_t>(bus)].store(qBound(0.0f, gain, 1.2f));
}

void AudioEngine::setPadAdsr(int padId, float attack, float decay, float sustain, float release) {
    if (padId < 0 || padId >= static_cast<int>(m_padAttack.size())) {
        return;
    }
    m_padAttack[static_cast<size_t>(padId)].store(qBound(0.0f, attack, 1.0f));
    m_padDecay[static_cast<size_t>(padId)].store(qBound(0.0f, decay, 1.0f));
    m_padSustain[static_cast<size_t>(padId)].store(qBound(0.0f, sustain, 1.0f));
    m_padRelease[static_cast<size_t>(padId)].store(qBound(0.0f, release, 1.0f));
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
    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        if (m_lastOutValid && static_cast<int>(m_lastOut.size()) == frames * m_channels) {
            std::copy(m_lastOut.begin(), m_lastOut.end(), out);
        } else {
            std::fill(out, out + frames * m_channels, 0.0f);
        }
        return;
    }
    for (auto &buffer : m_busBuffers) {
        buffer.assign(frames * m_channels, 0.0f);
    }

    for (auto it = m_voices.begin(); it != m_voices.end();) {
        Voice &voice = *it;
        if (!voice.buffer || !voice.buffer->isValid()) {
            it = m_voices.erase(it);
            continue;
        }

        const float *data = voice.buffer->samples.constData();
        const int channels = voice.buffer->channels;
        const int totalFrames = voice.buffer->frames();

        double pos = voice.position;
        bool done = false;
        const int padIndex = qBound(0, voice.padId, static_cast<int>(m_padAttack.size()) - 1);
        const float attack = m_padAttack[static_cast<size_t>(padIndex)].load();
        const float decay = m_padDecay[static_cast<size_t>(padIndex)].load();
        const float sustain = m_padSustain[static_cast<size_t>(padIndex)].load();
        const float release = m_padRelease[static_cast<size_t>(padIndex)].load();

        const float attackSec = 0.005f + attack * 1.2f;
        const float decaySec = 0.01f + decay * 1.2f;
        const float releaseSec = 0.02f + release * 1.6f;
        const float attackStep = attackSec > 0.0f ? 1.0f / (attackSec * m_sampleRate) : 1.0f;
        const float decayStep =
            decaySec > 0.0f ? (1.0f - sustain) / (decaySec * m_sampleRate) : 1.0f;
        const float releaseStep =
            releaseSec > 0.0f ? 1.0f / (releaseSec * m_sampleRate) : 1.0f;

        const int busIndex =
            std::max(0, std::min(static_cast<int>(m_busBuffers.size() - 1), voice.bus));
        float *busOut = m_busBuffers[static_cast<size_t>(busIndex)].data();
        for (int i = 0; i < frames; ++i) {
            if (voice.releaseRequested && voice.envStage != EnvStage::Release) {
                voice.envStage = EnvStage::Release;
            }
            if (pos >= voice.endFrame) {
                if (voice.loop) {
                    pos = static_cast<double>(voice.startFrame);
                } else {
                    done = true;
                    break;
                }
            }
            if (pos >= totalFrames) {
                done = true;
                break;
            }

            const int idx = static_cast<int>(pos);
            const double frac = pos - static_cast<double>(idx);
            const int next = std::min(idx + 1, voice.endFrame - 1);
            const int idxA = idx * channels;
            const int idxB = next * channels;
            float leftA = data[idxA];
            float rightA = (channels > 1 && idxA + 1 < voice.buffer->samples.size())
                               ? data[idxA + 1]
                               : leftA;
            float leftB = data[idxB];
            float rightB = (channels > 1 && idxB + 1 < voice.buffer->samples.size())
                               ? data[idxB + 1]
                               : leftB;
            float left = leftA + static_cast<float>((leftB - leftA) * frac);
            float right = rightA + static_cast<float>((rightB - rightA) * frac);
            float env = voice.env;
            switch (voice.envStage) {
                case EnvStage::Attack:
                    env += attackStep;
                    if (env >= 1.0f) {
                        env = 1.0f;
                        voice.envStage = EnvStage::Decay;
                    }
                    break;
                case EnvStage::Decay:
                    env -= decayStep;
                    if (env <= sustain || decaySec <= 0.0f) {
                        env = sustain;
                        voice.envStage = EnvStage::Sustain;
                    }
                    break;
                case EnvStage::Sustain:
                    env = sustain;
                    break;
                case EnvStage::Release:
                    env -= releaseStep * std::max(0.1f, env);
                    if (env <= 0.0005f || releaseSec <= 0.0f) {
                        env = 0.0f;
                        done = true;
                    }
                    break;
            }
            voice.env = env;
            busOut[i * m_channels] += left * voice.gainL * env;
            busOut[i * m_channels + 1] += right * voice.gainR * env;
            pos += voice.rate;
        }

        voice.position = pos;
        if (done) {
            it = m_voices.erase(it);
        } else {
            ++it;
        }
    }

    // Compute sidechain envelope from sum of all buses pre-effects.
    std::vector<float> master(frames * m_channels, 0.0f);
    for (const auto &buf : m_busBuffers) {
        for (int i = 0; i < frames * m_channels; ++i) {
            master[i] += buf[i];
        }
    }

    const float sideEnv = computeEnv(master.data(), frames);

    // Process buses 1..5 into master.
    for (size_t bus = 1; bus < m_busBuffers.size(); ++bus) {
        processBus(static_cast<int>(bus), m_busBuffers[bus].data(), frames, sideEnv);
        const float gain = m_busGains[bus].load();
        for (int i = 0; i < frames * m_channels; ++i) {
            m_busBuffers[bus][i] *= gain;
        }
        for (int i = 0; i < frames * m_channels; ++i) {
            master[i] += m_busBuffers[bus][i];
        }
        const float meter = computePeak(m_busBuffers[bus].data(), frames);
        m_busMeters[bus].store(meter);
    }

    // Process master chain (bus 0).
    if (!m_busBuffers.empty()) {
        processBus(0, master.data(), frames, sideEnv);
    }
    const float masterGain = m_busGains[0].load();
    for (int i = 0; i < frames * m_channels; ++i) {
        master[i] *= masterGain;
    }
    m_busMeters[0].store(computePeak(master.data(), frames));

    std::copy(master.begin(), master.end(), out);
    if (static_cast<int>(m_lastOut.size()) != frames * m_channels) {
        m_lastOut.resize(frames * m_channels);
    }
    std::copy(master.begin(), master.end(), m_lastOut.begin());
    m_lastOutValid = true;
}

float AudioEngine::computeEnv(const float *buffer, int frames) const {
    double sum = 0.0;
    const int count = frames * m_channels;
    for (int i = 0; i < count; ++i) {
        const float v = buffer[i];
        sum += static_cast<double>(v * v);
    }
    const double rms = (count > 0) ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
    return static_cast<float>(rms);
}

float AudioEngine::computePeak(const float *buffer, int frames) const {
    float peak = 0.0f;
    const int count = frames * m_channels;
    for (int i = 0; i < count; ++i) {
        const float v = std::fabs(buffer[i]);
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}

void AudioEngine::processBus(int busIndex, float *buffer, int frames, float sidechainEnv) {
    if (busIndex < 0 || busIndex >= static_cast<int>(m_busChains.size())) {
        return;
    }
    BusChain &chain = m_busChains[static_cast<size_t>(busIndex)];
    if (chain.effects.empty()) {
        return;
    }

    for (EffectState &fx : chain.effects) {
        const float p1 = safeParam(fx.p1);
        const float p2 = safeParam(fx.p2);
        const float p3 = safeParam(fx.p3);
        const float p4 = safeParam(fx.p4);
        const float p5 = safeParam(fx.p5);
        switch (fx.type) {
            case 1: {  // reverb
                if (fx.bufA.empty()) {
                    const int lenA = static_cast<int>(m_sampleRate * 0.031f);
                    const int lenB = static_cast<int>(m_sampleRate * 0.037f);
                    fx.bufA.assign(lenA * m_channels, 0.0f);
                    fx.bufB.assign(lenB * m_channels, 0.0f);
                    fx.indexA = 0;
                    fx.indexB = 0;
                }
                const float wet = p1;
                const float feedback = 0.4f + p2 * 0.5f;
                for (int i = 0; i < frames; ++i) {
                    for (int ch = 0; ch < m_channels; ++ch) {
                        const int idx = i * m_channels + ch;
                        const int ia = (fx.indexA * m_channels + ch) % fx.bufA.size();
                        const int ib = (fx.indexB * m_channels + ch) % fx.bufB.size();
                        const float a = fx.bufA[ia];
                        const float b = fx.bufB[ib];
                        fx.bufA[ia] = buffer[idx] + a * feedback;
                        fx.bufB[ib] = buffer[idx] + b * feedback;
                        const float mix = (a + b) * 0.5f;
                        buffer[idx] = buffer[idx] * (1.0f - wet) + mix * wet;
                    }
                    fx.indexA = (fx.indexA + 1) % (fx.bufA.size() / m_channels);
                    fx.indexB = (fx.indexB + 1) % (fx.bufB.size() / m_channels);
                }
                break;
            }
            case 2: {  // comp
                const float threshold = 0.15f + p1 * 0.5f;
                const float ratio = 1.5f + p2 * 6.5f;
                const float attack = 0.01f + p3 * 0.12f;
                const float release = 0.03f + p4 * 0.35f;
                for (int i = 0; i < frames; ++i) {
                    float env = 0.0f;
                    for (int ch = 0; ch < m_channels; ++ch) {
                        const float v = std::fabs(buffer[i * m_channels + ch]);
                        if (v > env) {
                            env = v;
                        }
                    }
                    const float coeff = (env > fx.env) ? attack : release;
                    fx.env = fx.env + (env - fx.env) * coeff;
                    float gain = 1.0f;
                    if (fx.env > threshold) {
                        const float over = fx.env / threshold;
                        gain = std::pow(over, -(ratio - 1.0f) / ratio);
                    }
                    const float makeup = (p5 >= 0.5f) ? 1.35f : 1.0f;
                    for (int ch = 0; ch < m_channels; ++ch) {
                        buffer[i * m_channels + ch] *= (gain * makeup);
                    }
                }
                break;
            }
            case 3: {  // dist
                const float drive = 1.0f + p1 * 6.0f;
                const float mix = p2;
                for (int i = 0; i < frames * m_channels; ++i) {
                    const float v = buffer[i] * drive;
                    const float d = std::tanh(v);
                    buffer[i] = buffer[i] * (1.0f - mix) + d * mix;
                }
                break;
            }
            case 4: {  // lofi
                const int hold = std::max(1, 1 + static_cast<int>(p2 * 7.0f));
                const float bits = 4.0f + p1 * 8.0f;
                const float step = 1.0f / std::pow(2.0f, bits);
                for (int i = 0; i < frames; ++i) {
                    if ((i % hold) == 0) {
                        fx.z1L = buffer[i * m_channels];
                        if (m_channels > 1) {
                            fx.z1R = buffer[i * m_channels + 1];
                        }
                    }
                    float left = std::round(fx.z1L / step) * step;
                    float right = (m_channels > 1) ? std::round(fx.z1R / step) * step : left;
                    buffer[i * m_channels] = left;
                    if (m_channels > 1) {
                        buffer[i * m_channels + 1] = right;
                    }
                }
                break;
            }
            case 5: {  // cassette
                const float noiseAmount = p1 * 0.05f;
                const float lpf = 0.05f + p2 * 0.3f;
                for (int i = 0; i < frames * m_channels; ++i) {
                    const float noise = (static_cast<float>(std::rand()) / RAND_MAX - 0.5f) *
                                        noiseAmount;
                    fx.z1L = fx.z1L + lpf * (buffer[i] - fx.z1L);
                    buffer[i] = std::tanh(fx.z1L + noise);
                }
                break;
            }
            case 6: {  // chorus
                if (fx.bufA.empty()) {
                    const int len = static_cast<int>(m_sampleRate * 0.03f);
                    fx.bufA.assign(len * m_channels, 0.0f);
                    fx.indexA = 0;
                    fx.phase = 0.0f;
                }
                const float depth = 0.002f + p1 * 0.008f;
                const float rate = 0.1f + p2 * 0.8f;
                const float mix = p3;
                for (int i = 0; i < frames; ++i) {
                    const float lfo = (std::sin(fx.phase) + 1.0f) * 0.5f;
                    const int delay = static_cast<int>((0.005f + depth * lfo) * m_sampleRate);
                    for (int ch = 0; ch < m_channels; ++ch) {
                        const int writeIdx = (fx.indexA * m_channels + ch) % fx.bufA.size();
                        fx.bufA[writeIdx] = buffer[i * m_channels + ch];
                        int readIndex = fx.indexA - delay;
                        if (readIndex < 0) {
                            readIndex += fx.bufA.size() / m_channels;
                        }
                        const int readIdx = (readIndex * m_channels + ch) % fx.bufA.size();
                        const float delayed = fx.bufA[readIdx];
                        buffer[i * m_channels + ch] =
                            buffer[i * m_channels + ch] * (1.0f - mix) + delayed * mix;
                    }
                    fx.indexA = (fx.indexA + 1) % (fx.bufA.size() / m_channels);
                    fx.phase += 2.0f * static_cast<float>(M_PI) * rate / m_sampleRate;
                    if (fx.phase > 2.0f * static_cast<float>(M_PI)) {
                        fx.phase -= 2.0f * static_cast<float>(M_PI);
                    }
                }
                break;
            }
            case 7: {  // eq (simple tilt)
                const float dbLow = (p1 - 0.5f) * 24.0f;
                const float dbMid1 = (p2 - 0.5f) * 24.0f;
                const float dbMid2 = (p3 - 0.5f) * 24.0f;
                const float dbHigh = (p4 - 0.5f) * 24.0f;

                const float gLow = std::pow(10.0f, dbLow / 20.0f);
                const float gMid1 = std::pow(10.0f, dbMid1 / 20.0f);
                const float gMid2 = std::pow(10.0f, dbMid2 / 20.0f);
                const float gHigh = std::pow(10.0f, dbHigh / 20.0f);

                auto alphaFor = [&](float hz) {
                    const float x = -2.0f * static_cast<float>(M_PI) * hz / m_sampleRate;
                    return std::exp(x);
                };
                const float aLow = alphaFor(200.0f);
                const float aHigh = alphaFor(4000.0f);
                const float aM1Low = alphaFor(300.0f);
                const float aM1High = alphaFor(1200.0f);
                const float aM2Low = alphaFor(1200.0f);
                const float aM2High = alphaFor(5000.0f);

                auto onePoleLow = [](float x, float &z, float a) {
                    z = a * z + (1.0f - a) * x;
                    return z;
                };
                auto onePoleHigh = [](float x, float &z, float a) {
                    z = a * z + (1.0f - a) * x;
                    return x - z;
                };

                for (int i = 0; i < frames; ++i) {
                    for (int ch = 0; ch < m_channels; ++ch) {
                        const int idx = i * m_channels + ch;
                        float x = buffer[idx];

                        float low = (ch == 0)
                                        ? onePoleLow(x, fx.eqLowL, aLow)
                                        : onePoleLow(x, fx.eqLowR, aLow);
                        float high = (ch == 0)
                                         ? onePoleHigh(x, fx.eqHighL, aHigh)
                                         : onePoleHigh(x, fx.eqHighR, aHigh);

                        float m1lp = (ch == 0)
                                         ? onePoleLow(x, fx.eqLp1L, aM1High)
                                         : onePoleLow(x, fx.eqLp1R, aM1High);
                        float mid1 = (ch == 0)
                                         ? onePoleHigh(m1lp, fx.eqHp1L, aM1Low)
                                         : onePoleHigh(m1lp, fx.eqHp1R, aM1Low);

                        float m2lp = (ch == 0)
                                         ? onePoleLow(x, fx.eqLp2L, aM2High)
                                         : onePoleLow(x, fx.eqLp2R, aM2High);
                        float mid2 = (ch == 0)
                                         ? onePoleHigh(m2lp, fx.eqHp2L, aM2Low)
                                         : onePoleHigh(m2lp, fx.eqHp2R, aM2Low);

                        const float out = x + (gLow - 1.0f) * low + (gMid1 - 1.0f) * mid1 +
                                          (gMid2 - 1.0f) * mid2 + (gHigh - 1.0f) * high;
                        buffer[idx] = out;
                    }
                }
                break;
            }
            case 8: {  // sidechain
                const float threshold = 0.05f + p1 * 0.2f;
                const float amount = p2;
                float gain = 1.0f;
                if (sidechainEnv > threshold) {
                    const float over = (sidechainEnv - threshold) / (1.0f - threshold);
                    gain = 1.0f - amount * over;
                }
                for (int i = 0; i < frames * m_channels; ++i) {
                    buffer[i] *= gain;
                }
                break;
            }
            default:
                break;
        }
    }
}
