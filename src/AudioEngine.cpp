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
#include <QDataStream>

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
    {
        bool ok = false;
        const int envFrames = qEnvironmentVariableIntValue("GROOVEBOX_PERIOD_FRAMES", &ok);
        if (ok) {
            m_periodFrames = qBound(64, envFrames, 2048);
        }
    }
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
    for (auto &state : m_synthStates) {
        state.fmParams.macros.fill(0.5f);
    }
    for (auto &ph : m_padPlayheads) {
        ph.store(-1.0f);
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
        auto detectPreferred = []() -> QString {
#ifdef Q_OS_LINUX
            QFile file("/proc/asound/cards");
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                return QString();
            }
            QString usbCard;
            QString phonesCard;
            while (!file.atEnd()) {
                const QString line = QString::fromUtf8(file.readLine()).trimmed();
                if (line.isEmpty()) {
                    continue;
                }
                if (line.contains("USB Audio", Qt::CaseInsensitive) ||
                    line.contains("CODEC", Qt::CaseInsensitive) ||
                    line.contains("UMC", Qt::CaseInsensitive) ||
                    line.contains("BEHRINGER", Qt::CaseInsensitive)) {
                    const QString index = line.section(' ', 0, 0).trimmed();
                    if (!index.isEmpty() && index[0].isDigit()) {
                        usbCard = index;
                    }
                }
                if (line.contains("Headphones", Qt::CaseInsensitive)) {
                    const QString index = line.section(' ', 0, 0).trimmed();
                    if (!index.isEmpty() && index[0].isDigit()) {
                        phonesCard = index;
                    }
                }
            }
            if (!usbCard.isEmpty()) {
                return usbCard;
            }
            if (!phonesCard.isEmpty()) {
                return phonesCard;
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
        const QString card = detectPreferred();
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
    voice.useEnv = (padId >= 0);
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

float AudioEngine::padPlayhead(int padId) const {
    if (!m_available) {
        return -1.0f;
    }
    if (padId < 0 || padId >= static_cast<int>(m_padPlayheads.size())) {
        return -1.0f;
    }
    return m_padPlayheads[static_cast<size_t>(padId)].load(std::memory_order_relaxed);
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
    bool hasSidechain = false;
    for (const auto &busChain : m_busChains) {
        for (const auto &fx : busChain.effects) {
            if (fx.type == 8) {
                hasSidechain = true;
                break;
            }
        }
        if (hasSidechain) {
            break;
        }
    }
    m_hasSidechain.store(hasSidechain);
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

void AudioEngine::setBpm(int bpm) {
    const int next = qBound(30, bpm, 300);
    m_bpm.store(static_cast<float>(next));
}

static bool writeWavFile(const QString &path, const std::vector<float> &samples,
                         int srcRate, int targetRate, int channels) {
    if (samples.empty() || channels <= 0) {
        return false;
    }
    int srcFrames = static_cast<int>(samples.size() / channels);
    if (srcFrames <= 0) {
        return false;
    }
    if (targetRate <= 0) {
        targetRate = srcRate;
    }
    std::vector<float> resampled;
    const float rateRatio = static_cast<float>(targetRate) / static_cast<float>(srcRate);
    int dstFrames = srcFrames;
    if (targetRate != srcRate) {
        dstFrames = qMax(1, static_cast<int>(std::floor(srcFrames * rateRatio)));
        resampled.resize(static_cast<size_t>(dstFrames * channels));
        for (int i = 0; i < dstFrames; ++i) {
            const float srcPos = i / rateRatio;
            const int i0 = qBound(0, static_cast<int>(std::floor(srcPos)), srcFrames - 1);
            const int i1 = qMin(srcFrames - 1, i0 + 1);
            const float frac = srcPos - i0;
            for (int ch = 0; ch < channels; ++ch) {
                const float s0 = samples[static_cast<size_t>(i0 * channels + ch)];
                const float s1 = samples[static_cast<size_t>(i1 * channels + ch)];
                resampled[static_cast<size_t>(i * channels + ch)] = s0 + (s1 - s0) * frac;
            }
        }
    } else {
        resampled = samples;
    }

    const int totalFrames = static_cast<int>(resampled.size() / channels);
    QByteArray pcm;
    pcm.resize(totalFrames * channels * 2);
    int16_t *out = reinterpret_cast<int16_t *>(pcm.data());
    for (int i = 0; i < totalFrames * channels; ++i) {
        float v = resampled[static_cast<size_t>(i)];
        v = std::max(-1.0f, std::min(1.0f, v));
        out[i] = static_cast<int16_t>(v * 32767.0f);
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const int byteRate = targetRate * channels * 2;
    const int blockAlign = channels * 2;
    const int dataSize = pcm.size();
    const int riffSize = 36 + dataSize;

    QDataStream ds(&file);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.writeRawData("RIFF", 4);
    ds << riffSize;
    ds.writeRawData("WAVE", 4);
    ds.writeRawData("fmt ", 4);
    ds << 16;
    ds << static_cast<quint16>(1);
    ds << static_cast<quint16>(channels);
    ds << static_cast<quint32>(targetRate);
    ds << static_cast<quint32>(byteRate);
    ds << static_cast<quint16>(blockAlign);
    ds << static_cast<quint16>(16);
    ds.writeRawData("data", 4);
    ds << static_cast<quint32>(dataSize);
    file.write(pcm);
    file.close();
    return true;
}

bool AudioEngine::startRecording(const QString &path, int totalFrames, int targetSampleRate) {
    if (totalFrames <= 0 || path.isEmpty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_recordMutex);
    m_recordFloat.clear();
    m_recordFloat.reserve(static_cast<size_t>(totalFrames * m_channels));
    m_recordFramesLeft = totalFrames;
    m_recordTargetRate = targetSampleRate;
    m_recordPath = path;
    m_recording.store(true);
    return true;
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

void AudioEngine::setSynthEnabled(int padId, bool enabled) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    state.enabled = enabled;
    if (!enabled) {
        for (size_t n = 0; n < state.activeNotes.size(); ++n) {
            if (state.activeNotes[n]) {
                state.core.noteOff(static_cast<int>(n));
                state.activeNotes[n] = false;
            }
        }
        state.env = 0.0f;
        state.envStage = EnvStage::Attack;
        state.releaseRequested = false;
    } else {
        ensureSynthInit(state);
    }
}

void AudioEngine::setSynthKind(int padId, SynthKind kind) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    if (state.kind == kind) {
        return;
    }
    state.kind = kind;
    state.initialized = false;
    state.bankLoaded = false;
    state.env = 0.0f;
    state.envStage = EnvStage::Attack;
    state.releaseRequested = false;
    state.lfoPhase = 0.0f;
    state.filterIc1L = 0.0f;
    state.filterIc2L = 0.0f;
    state.filterIc1R = 0.0f;
    state.filterIc2R = 0.0f;
    for (bool &note : state.activeNotes) {
        note = false;
    }
    if (state.enabled) {
        ensureSynthInit(state);
    }
}

void AudioEngine::setSynthParams(int padId, float volume, float pan, int bus) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    float gainL = 1.0f;
    float gainR = 1.0f;
    computePanGains(pan, volume, gainL, gainR);
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    state.gainL = gainL;
    state.gainR = gainR;
    state.bus = std::max(0, std::min(static_cast<int>(m_busBuffers.size() - 1), bus));
}

void AudioEngine::setFmParams(int padId, const FmParams &params) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    state.fmParams = params;
    auto macroShift = [](float base, float macro, float amount) {
        const float shifted = base + (macro - 0.5f) * amount;
        return std::max(0.0f, std::min(1.0f, shifted));
    };
    const float fm = macroShift(params.fmAmount, params.macros[0], 0.6f);
    const float ratio = std::max(0.1f, params.ratio + (params.macros[1] - 0.5f) * 4.0f);
    const float feedback = macroShift(params.feedback, params.macros[2], 0.6f);
    const float cutoff = macroShift(params.cutoff, params.macros[3], 0.7f);
    const float resonance = macroShift(params.resonance, params.macros[4], 0.7f);
    const float lfoDepth = macroShift(params.lfoDepth, params.macros[5], 0.7f);
    const float lfoRate = std::max(0.01f, params.lfoRate + (params.macros[6] - 0.5f) * 0.6f);

    SimpleFmCore::Params fmParams;
    fmParams.fmAmount = fm;
    fmParams.ratio = ratio;
    fmParams.feedback = feedback;
    fmParams.osc1Wave = params.osc1Wave;
    fmParams.osc2Wave = params.osc2Wave;
    fmParams.osc1Voices = params.osc1Voices;
    fmParams.osc2Voices = params.osc2Voices;
    fmParams.osc1Detune = params.osc1Detune;
    fmParams.osc2Detune = params.osc2Detune;
    fmParams.osc1Gain = params.osc1Gain;
    fmParams.osc2Gain = params.osc2Gain;
    fmParams.osc1Pan = params.osc1Pan;
    fmParams.osc2Pan = params.osc2Pan;
    state.fm.setParams(fmParams);
    state.filterCutoff = cutoff;
    state.filterResonance = resonance;
    state.filterType = params.filterType;
    state.lfoRate = lfoRate;
    state.lfoDepth = lfoDepth;
}

void AudioEngine::setSynthVoices(int padId, int voices) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    voices = std::max(1, std::min(16, voices));
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    if (state.voices == voices) {
        return;
    }
    state.voices = voices;
    state.initialized = false;
    state.bankLoaded = false;
    if (state.enabled) {
        ensureSynthInit(state);
    }
}

void AudioEngine::synthNoteOn(int padId, int midiNote, int velocity) {
    if (!m_available) {
        return;
    }
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    if (midiNote < 0 || midiNote > 127) {
        return;
    }
    velocity = std::max(1, std::min(127, velocity));
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    if (!state.enabled) {
        return;
    }
    const bool hadActive =
        std::any_of(state.activeNotes.begin(), state.activeNotes.end(), [](bool v) { return v; });
    ensureSynthInit(state);
    if (state.kind == SynthKind::SimpleFm) {
        state.fm.noteOn(midiNote, velocity);
    } else {
        state.core.noteOn(midiNote, velocity);
    }
    state.activeNotes[static_cast<size_t>(midiNote)] = true;
    if (!hadActive || state.envStage == EnvStage::Release) {
        state.envStage = EnvStage::Attack;
        state.releaseRequested = false;
    }
}

void AudioEngine::synthNoteOff(int padId, int midiNote) {
    if (!m_available) {
        return;
    }
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    if (midiNote < 0 || midiNote > 127) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    if (!state.enabled) {
        return;
    }
    ensureSynthInit(state);
    if (state.kind == SynthKind::SimpleFm) {
        state.fm.noteOff(midiNote);
    } else {
        state.core.noteOff(midiNote);
    }
    state.activeNotes[static_cast<size_t>(midiNote)] = false;
    const bool hasActive =
        std::any_of(state.activeNotes.begin(), state.activeNotes.end(), [](bool v) { return v; });
    if (!hasActive) {
        state.releaseRequested = true;
    }
}

void AudioEngine::synthAllNotesOff(int padId) {
    if (!m_available) {
        return;
    }
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    if (!state.enabled) {
        return;
    }
    ensureSynthInit(state);
    for (size_t n = 0; n < state.activeNotes.size(); ++n) {
        if (state.activeNotes[n]) {
            if (state.kind == SynthKind::SimpleFm) {
                state.fm.noteOff(static_cast<int>(n));
            } else {
                state.core.noteOff(static_cast<int>(n));
            }
            state.activeNotes[n] = false;
        }
    }
    state.releaseRequested = true;
}

bool AudioEngine::loadSynthSysex(int padId, const QString &path) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    const QString prevPath = state.bankPath;
    state.bankPath = path;
    if (path.isEmpty()) {
        state.bankLoaded = false;
        return false;
    }
    const bool wasInitialized = state.initialized;
    ensureSynthInit(state);
    if (wasInitialized && (prevPath != path || !state.bankLoaded)) {
        state.bankLoaded = state.core.loadSysexFile(path.toStdString());
    }
    if (!state.bankLoaded) {
        return false;
    }
    const int count = state.core.programCount();
    if (count > 0) {
        state.programIndex = qBound(0, state.programIndex, count - 1);
        state.core.selectProgram(state.programIndex);
    }
    return true;
}

bool AudioEngine::setSynthProgram(int padId, int program) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    state.programIndex = std::max(0, program);
    ensureSynthInit(state);
    const int count = state.core.programCount();
    if (count <= 0) {
        return false;
    }
    state.programIndex = qBound(0, state.programIndex, count - 1);
    return state.core.selectProgram(state.programIndex);
}

int AudioEngine::synthProgramCount(int padId) const {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    return state.core.programCount();
}

QString AudioEngine::synthProgramName(int padId, int index) const {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return QString();
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    return QString::fromUtf8(state.core.programName(index));
}

int AudioEngine::synthVoiceParam(int padId, int param) const {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    return state.core.voiceParam(param);
}

bool AudioEngine::setSynthVoiceParam(int padId, int param, int value) {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    ensureSynthInit(state);
    return state.core.setVoiceParam(param, value);
}

bool AudioEngine::isSynthActive(int padId) const {
    if (padId < 0 || padId >= static_cast<int>(m_synthStates.size())) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const SynthState &state = m_synthStates[static_cast<size_t>(padId)];
    if (!state.enabled) {
        return false;
    }
    for (bool active : state.activeNotes) {
        if (active) {
            return true;
        }
    }
    return state.env > 0.0005f;
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

void AudioEngine::ensureSynthInit(SynthState &state) {
    if (state.initialized) {
        return;
    }
    if (state.kind == SynthKind::SimpleFm) {
        state.fm.init(m_sampleRate, state.voices);
        SimpleFmCore::Params fmParams;
        fmParams.fmAmount = state.fmParams.fmAmount;
        fmParams.ratio = state.fmParams.ratio;
        fmParams.feedback = state.fmParams.feedback;
        fmParams.osc1Wave = state.fmParams.osc1Wave;
        fmParams.osc2Wave = state.fmParams.osc2Wave;
        fmParams.osc1Voices = state.fmParams.osc1Voices;
        fmParams.osc2Voices = state.fmParams.osc2Voices;
        fmParams.osc1Detune = state.fmParams.osc1Detune;
        fmParams.osc2Detune = state.fmParams.osc2Detune;
        fmParams.osc1Gain = state.fmParams.osc1Gain;
        fmParams.osc2Gain = state.fmParams.osc2Gain;
        fmParams.osc1Pan = state.fmParams.osc1Pan;
        fmParams.osc2Pan = state.fmParams.osc2Pan;
        state.fm.setParams(fmParams);
        state.initialized = true;
        return;
    }

    state.core.init(m_sampleRate, state.voices);
    state.initialized = true;
    state.bankLoaded = false;
    if (!state.bankPath.isEmpty()) {
        state.bankLoaded = state.core.loadSysexFile(state.bankPath.toStdString());
        if (state.bankLoaded) {
            const int count = state.core.programCount();
            if (count > 0) {
                state.programIndex = qBound(0, state.programIndex, count - 1);
                state.core.selectProgram(state.programIndex);
            }
        }
    }
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

    std::array<float, 8> padPlayhead{};
    padPlayhead.fill(-1.0f);

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
            if (voice.useEnv) {
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
            } else {
                env = 1.0f;
            }
            busOut[i * m_channels] += left * voice.gainL * env;
            busOut[i * m_channels + 1] += right * voice.gainR * env;
            pos += voice.rate;
        }

        voice.position = pos;
        if (voice.padId >= 0 && voice.padId < static_cast<int>(padPlayhead.size())) {
            const int denom = std::max(1, voice.endFrame - voice.startFrame);
            float ratio = static_cast<float>((pos - voice.startFrame) / static_cast<double>(denom));
            ratio = std::max(0.0f, std::min(1.0f, ratio));
            const size_t padIdx = static_cast<size_t>(voice.padId);
            if (padPlayhead[padIdx] < 0.0f) {
                padPlayhead[padIdx] = ratio;
            } else if (ratio > padPlayhead[padIdx]) {
                padPlayhead[padIdx] = ratio;
            }
        }
        if (done) {
            it = m_voices.erase(it);
        } else {
            ++it;
        }
    }

    for (size_t i = 0; i < m_padPlayheads.size(); ++i) {
        m_padPlayheads[i].store(padPlayhead[i], std::memory_order_relaxed);
    }

    if (frames > 0 && !m_synthStates.empty()) {
        if (static_cast<int>(m_synthScratchL.size()) != frames) {
            m_synthScratchL.assign(frames, 0.0f);
            m_synthScratchR.assign(frames, 0.0f);
        }
        for (size_t pad = 0; pad < m_synthStates.size(); ++pad) {
            SynthState &synth = m_synthStates[pad];
            if (!synth.enabled) {
                continue;
            }
            ensureSynthInit(synth);
            const int busIndex =
                std::max(0, std::min(static_cast<int>(m_busBuffers.size() - 1), synth.bus));
            float *busOut = m_busBuffers[static_cast<size_t>(busIndex)].data();

            const int padIndex = qBound(0, static_cast<int>(pad),
                                        static_cast<int>(m_padAttack.size()) - 1);
            const float attack = m_padAttack[static_cast<size_t>(padIndex)].load();
            const float decay = m_padDecay[static_cast<size_t>(padIndex)].load();
            const float sustain = m_padSustain[static_cast<size_t>(padIndex)].load();
            const float release = m_padRelease[static_cast<size_t>(padIndex)].load();

            const float attackSec = 0.005f + attack * 1.2f;
            const float decaySec = 0.01f + decay * 1.2f;
            const float releaseSec = 0.02f + release * 1.6f;
            const float attackStep =
                attackSec > 0.0f ? 1.0f / (attackSec * m_sampleRate) : 1.0f;
            const float decayStep =
                decaySec > 0.0f ? (1.0f - sustain) / (decaySec * m_sampleRate) : 1.0f;
            const float releaseStep =
                releaseSec > 0.0f ? 1.0f / (releaseSec * m_sampleRate) : 1.0f;

            const bool isDx7 = (synth.kind == SynthKind::Dx7);
            const bool neutralEnv =
                (attack <= 0.001f && decay <= 0.001f && release <= 0.001f && sustain >= 0.999f);
            const bool useExternalEnv = !(isDx7 && neutralEnv);

            const bool hasNotes = std::any_of(synth.activeNotes.begin(),
                                             synth.activeNotes.end(),
                                             [](bool v) { return v; });
            if (!hasNotes && !synth.releaseRequested) {
                synth.env = 0.0f;
                synth.envStage = EnvStage::Attack;
                continue;
            }
            if (synth.kind == SynthKind::SimpleFm) {
                synth.fm.render(m_synthScratchL.data(), m_synthScratchR.data(), frames);
            } else {
                synth.core.render(m_synthScratchL.data(), m_synthScratchR.data(), frames);
            }
            // Tiny stereo spread: 1-sample delay on right to avoid mono collapse.
            float prevR = synth.stereoDelay;
            for (int i = 0; i < frames; ++i) {
                const float r = m_synthScratchR[i];
                m_synthScratchR[i] = r * 0.7f + prevR * 0.3f;
                prevR = r;
            }
            synth.stereoDelay = prevR;

            const bool useFilter = (synth.kind == SynthKind::SimpleFm);
            const float baseCutoff = synth.filterCutoff;
            const float baseRes = synth.filterResonance;
            const float lfoDepth = synth.lfoDepth;
            const float lfoRateHz = 0.1f + synth.lfoRate * 8.0f;
            const float lfoInc = 2.0f * static_cast<float>(M_PI) * lfoRateHz / m_sampleRate;
            float staticG = 0.0f;
            float staticR = 0.0f;
            if (useFilter && lfoDepth <= 0.0001f) {
                const float cutoff = std::max(0.02f, std::min(0.98f, baseCutoff));
                const float hz = 40.0f * std::pow(2.0f, cutoff * 8.0f);
                const float g = std::tan(static_cast<float>(M_PI) * hz / m_sampleRate);
                const float q = 0.7f + baseRes * 7.0f;
                staticG = g;
                staticR = 1.0f / (2.0f * q);
            }

            float dx7Peak = 0.0f;
            for (int i = 0; i < frames; ++i) {
                float env = 1.0f;
                if (useExternalEnv) {
                    if (synth.releaseRequested && !hasNotes &&
                        synth.envStage != EnvStage::Release) {
                        synth.envStage = EnvStage::Release;
                    }
                    env = synth.env;
                    switch (synth.envStage) {
                        case EnvStage::Attack:
                            env += attackStep;
                            if (env >= 1.0f) {
                                env = 1.0f;
                                synth.envStage = EnvStage::Decay;
                            }
                            break;
                        case EnvStage::Decay:
                            env -= decayStep;
                            if (env <= sustain || decaySec <= 0.0f) {
                                env = sustain;
                                synth.envStage = EnvStage::Sustain;
                            }
                            break;
                        case EnvStage::Sustain:
                            env = sustain;
                            break;
                        case EnvStage::Release:
                            env -= releaseStep * std::max(0.1f, env);
                            if (env <= 0.0005f || releaseSec <= 0.0f) {
                                env = 0.0f;
                                synth.releaseRequested = false;
                            }
                            break;
                    }
                    synth.env = env;
                } else {
                    synth.envStage = EnvStage::Sustain;
                    synth.env = 1.0f;
                }

                float left = m_synthScratchL[static_cast<size_t>(i)];
                float right = m_synthScratchR[static_cast<size_t>(i)];
                if (useFilter) {
                    float g = staticG;
                    float R = staticR;
                    if (lfoDepth > 0.0001f) {
                        const float lfo = std::sin(synth.lfoPhase);
                        synth.lfoPhase += lfoInc;
                        if (synth.lfoPhase > 2.0f * static_cast<float>(M_PI)) {
                            synth.lfoPhase -= 2.0f * static_cast<float>(M_PI);
                        }
                        const float cutoff =
                            std::max(0.02f, std::min(0.98f, baseCutoff + lfo * lfoDepth * 0.5f));
                        const float hz = 40.0f * std::pow(2.0f, cutoff * 8.0f);
                        g = std::tan(static_cast<float>(M_PI) * hz / m_sampleRate);
                        const float q = 0.7f + baseRes * 7.0f;
                        R = 1.0f / (2.0f * q);
                    }
                    if (g > 0.0f) {
                        auto svf = [&](float input, float &ic1, float &ic2, float &low,
                                       float &band, float &high) {
                            const float v3 = input - ic2;
                            const float v1 = (g * v3 + ic1) / (1.0f + g * (g + R));
                            const float v2 = ic2 + g * v1;
                            ic1 = 2.0f * v1 - ic1;
                            ic2 = 2.0f * v2 - ic2;
                            low = v2;
                            band = v1;
                            high = v3 - R * v1 - v2;
                        };

                        float lowL = 0.0f, bandL = 0.0f, highL = 0.0f;
                        float lowR = 0.0f, bandR = 0.0f, highR = 0.0f;
                        svf(left, synth.filterIc1L, synth.filterIc2L, lowL, bandL, highL);
                        svf(right, synth.filterIc1R, synth.filterIc2R, lowR, bandR, highR);

                        auto applyFilterMode = [&](float input, float low, float band, float high) {
                            switch (synth.filterType) {
                                case 0: // lowpass
                                    return low;
                                case 1: // highpass
                                    return high;
                                case 2: // bandpass
                                    return band;
                                case 3: // notch
                                    return low + high;
                                case 4: // peak
                                    return band;
                                case 5: // low shelf
                                    return input + low * 0.6f;
                                case 6: // high shelf
                                    return input + high * 0.6f;
                                case 7: // allpass (approx)
                                    return input - 2.0f * R * band;
                                case 8: // bypass
                                    return input;
                                case 9: // low+mid
                                    return low + band;
                                default:
                                    return low;
                            }
                        };

                        left = applyFilterMode(left, lowL, bandL, highL);
                        right = applyFilterMode(right, lowR, bandR, highR);
                    }
                }

                const int idx = i * m_channels;
                busOut[idx] += left * synth.gainL * env;
                if (m_channels > 1) {
                    busOut[idx + 1] += right * synth.gainR * env;
                }
                if (isDx7 && !useExternalEnv && !hasNotes) {
                    const float peakL = std::fabs(left * synth.gainL);
                    const float peakR = std::fabs(right * synth.gainR);
                    dx7Peak = std::max(dx7Peak, std::max(peakL, peakR));
                }
            }
            if (isDx7 && !useExternalEnv && !hasNotes) {
                if (dx7Peak < 0.00008f) {
                    synth.releaseRequested = false;
                    synth.env = 0.0f;
                }
            }
        }
    }

    // Compute sidechain envelope from sum of all buses pre-effects.
    std::vector<float> drySum(frames * m_channels, 0.0f);
    for (const auto &buf : m_busBuffers) {
        for (int i = 0; i < frames * m_channels; ++i) {
            drySum[i] += buf[i];
        }
    }

    float sideEnv = 0.0f;
    if (m_hasSidechain.load()) {
        sideEnv = computeEnv(drySum.data(), frames);
    }

    // Start master with bus 0 only (avoid double-counting).
    std::vector<float> master(frames * m_channels, 0.0f);
    if (!m_busBuffers.empty()) {
        std::copy(m_busBuffers[0].begin(), m_busBuffers[0].end(), master.begin());
    }

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

    // Recording tap (float).
    if (m_recording.load()) {
        std::lock_guard<std::mutex> lock(m_recordMutex);
        if (m_recording.load() && m_recordFramesLeft > 0) {
            const int framesToWrite = std::min(frames, m_recordFramesLeft);
            m_recordFloat.insert(m_recordFloat.end(), master.begin(),
                                 master.begin() + framesToWrite * m_channels);
            m_recordFramesLeft -= framesToWrite;
            if (m_recordFramesLeft <= 0) {
                m_recording.store(false);
                writeWavFile(m_recordPath, m_recordFloat, m_sampleRate, m_recordTargetRate,
                             m_channels);
                m_recordFloat.clear();
            }
        }
    }

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
            case 7: {  // eq (low/high cut)
                float lowCut = 30.0f * std::pow(2.0f, p1 * 5.5f);
                float highCut = 800.0f * std::pow(2.0f, p2 * 4.5f);
                lowCut = std::min(lowCut, 4000.0f);
                highCut = std::min(highCut, static_cast<float>(m_sampleRate * 0.45f));
                if (highCut < lowCut * 1.5f) {
                    highCut = lowCut * 1.5f;
                }

                auto alphaFor = [&](float hz) {
                    const float x = -2.0f * static_cast<float>(M_PI) * hz / m_sampleRate;
                    return std::exp(x);
                };
                const float aLow = alphaFor(lowCut);
                const float aHigh = alphaFor(highCut);

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
                        float hp = (ch == 0)
                                       ? onePoleHigh(x, fx.eqLowL, aLow)
                                       : onePoleHigh(x, fx.eqLowR, aLow);
                        float lp = (ch == 0)
                                       ? onePoleLow(hp, fx.eqHighL, aHigh)
                                       : onePoleLow(hp, fx.eqHighR, aHigh);
                        buffer[idx] = lp;
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
            case 9: {  // delay
                const float maxDelay = 0.9f;
                const float timeSec = 0.03f + p1 * maxDelay;
                const int delaySamples = std::max(1, static_cast<int>(timeSec * m_sampleRate));
                const bool stereo = (p4 >= 0.5f);
                const float feedback = 0.1f + p2 * 0.85f;
                const float mix = p3;
                const int needed = delaySamples * m_channels;
                if (fx.bufA.size() < needed) {
                    fx.bufA.assign(needed, 0.0f);
                    fx.indexA = 0;
                }
                const int framesDelay = fx.bufA.size() / m_channels;
                for (int i = 0; i < frames; ++i) {
                    const int readIndex = (fx.indexA - delaySamples + framesDelay) % framesDelay;
                    const int writeIndex = fx.indexA;
                    float inL = buffer[i * m_channels];
                    float inR = (m_channels > 1) ? buffer[i * m_channels + 1] : inL;
                    float dl = fx.bufA[readIndex * m_channels];
                    float dr = (m_channels > 1) ? fx.bufA[readIndex * m_channels + 1] : dl;
                    float fbL = stereo ? dr : dl;
                    float fbR = stereo ? dl : dr;
                    fx.bufA[writeIndex * m_channels] = inL + fbL * feedback;
                    if (m_channels > 1) {
                        fx.bufA[writeIndex * m_channels + 1] = inR + fbR * feedback;
                    }
                    buffer[i * m_channels] = inL * (1.0f - mix) + dl * mix;
                    if (m_channels > 1) {
                        buffer[i * m_channels + 1] = inR * (1.0f - mix) + dr * mix;
                    }
                    fx.indexA = (fx.indexA + 1) % framesDelay;
                }
                break;
            }
            case 10: {  // tremolo
                const float depth = p1;
                const bool sync = (p3 >= 0.5f);
                float rate = 0.5f + p2 * 6.0f;
                if (sync) {
                    const float bpm = std::max(30.0f, m_bpm.load());
                    const float base = bpm / 60.0f;
                    const int divIndex = qBound(0, static_cast<int>(p2 * 4.99f), 4);
                    static const float mults[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
                    rate = base * mults[divIndex];
                }
                for (int i = 0; i < frames; ++i) {
                    const float lfo = (std::sin(fx.phase) + 1.0f) * 0.5f;
                    const float gain = 1.0f - depth * (1.0f - lfo);
                    for (int ch = 0; ch < m_channels; ++ch) {
                        buffer[i * m_channels + ch] *= gain;
                    }
                    fx.phase += 2.0f * static_cast<float>(M_PI) * rate / m_sampleRate;
                    if (fx.phase > 2.0f * static_cast<float>(M_PI)) {
                        fx.phase -= 2.0f * static_cast<float>(M_PI);
                    }
                }
                break;
            }
            case 11: {  // ring modulation
                const float freq = 50.0f * std::pow(2.0f, p1 * 5.0f);
                const float mix = p2;
                for (int i = 0; i < frames; ++i) {
                    const float mod = std::sin(fx.phase);
                    fx.phase += 2.0f * static_cast<float>(M_PI) * freq / m_sampleRate;
                    if (fx.phase > 2.0f * static_cast<float>(M_PI)) {
                        fx.phase -= 2.0f * static_cast<float>(M_PI);
                    }
                    for (int ch = 0; ch < m_channels; ++ch) {
                        const int idx = i * m_channels + ch;
                        const float dry = buffer[idx];
                        const float wet = dry * mod;
                        buffer[idx] = dry * (1.0f - mix) + wet * mix;
                    }
                }
                break;
            }
            case 12: {  // robot (short delay/comb)
                const float timeSec = 0.002f + p1 * 0.02f;
                const float feedback = p2 * 0.6f;
                const float mix = p3;
                const int delaySamples = std::max(1, static_cast<int>(timeSec * m_sampleRate));
                const int needed = delaySamples * m_channels;
                if (fx.bufA.size() < needed) {
                    fx.bufA.assign(needed, 0.0f);
                    fx.indexA = 0;
                }
                const int framesDelay = fx.bufA.size() / m_channels;
                for (int i = 0; i < frames; ++i) {
                    const int readIndex = (fx.indexA - delaySamples + framesDelay) % framesDelay;
                    for (int ch = 0; ch < m_channels; ++ch) {
                        const int idx = i * m_channels + ch;
                        const float delayed = fx.bufA[readIndex * m_channels + ch];
                        const float dry = buffer[idx];
                        const float wet = dry + delayed * feedback;
                        fx.bufA[fx.indexA * m_channels + ch] = wet;
                        buffer[idx] = dry * (1.0f - mix) + wet * mix;
                    }
                    fx.indexA = (fx.indexA + 1) % framesDelay;
                }
                break;
            }
            case 13: {  // punch (transient boost)
                const float amount = 1.0f + p1 * 2.5f;
                const float attack = 0.2f + p2 * 0.6f;
                const float release = 0.02f + p3 * 0.2f;
                for (int i = 0; i < frames; ++i) {
                    float env = 0.0f;
                    for (int ch = 0; ch < m_channels; ++ch) {
                        env += std::fabs(buffer[i * m_channels + ch]);
                    }
                    env /= std::max(1, m_channels);
                    fx.z1L += (env - fx.z1L) * attack;
                    fx.env += (env - fx.env) * release;
                    const float transient = std::max(0.0f, fx.z1L - fx.env);
                    const float gain = 1.0f + transient * amount;
                    for (int ch = 0; ch < m_channels; ++ch) {
                        buffer[i * m_channels + ch] *= gain;
                    }
                }
                break;
            }
            case 14: {  // subharmonic generator
                const float amount = p1 * 0.7f;
                auto onePoleLow = [](float x, float &z, float a) {
                    z = a * z + (1.0f - a) * x;
                    return z;
                };
                const float a = std::exp(-2.0f * static_cast<float>(M_PI) * 180.0f / m_sampleRate);
                if (fx.env == 0.0f) {
                    fx.env = 1.0f;
                }
                for (int i = 0; i < frames; ++i) {
                    for (int ch = 0; ch < m_channels; ++ch) {
                        const int idx = i * m_channels + ch;
                        const float x = buffer[idx];
                        const float prev = (ch == 0) ? fx.z1L : fx.z1R;
                        if ((x >= 0.0f && prev < 0.0f) || (x < 0.0f && prev >= 0.0f)) {
                            fx.env = -fx.env;
                        }
                        if (ch == 0) {
                            fx.z1L = x;
                        } else {
                            fx.z1R = x;
                        }
                        float sub = fx.env * std::fabs(x);
                        sub = (ch == 0) ? onePoleLow(sub, fx.eqLowL, a)
                                        : onePoleLow(sub, fx.eqLowR, a);
                        buffer[idx] = x + sub * amount;
                    }
                }
                break;
            }
            case 15: {  // key harmonizer (simple pitch-shifted voices)
                const float mix = 0.2f + p1 * 0.8f;
                const int keyIndex = qBound(0, static_cast<int>(p2 * 11.99f), 11);
                const bool minor = (p3 >= 0.5f);
                const int third = minor ? 3 : 4;
                const int fifth = 7;
                int interval1 = third + keyIndex;
                int interval2 = fifth + keyIndex;
                if (interval1 >= 12) {
                    interval1 -= 12;
                }
                if (interval2 >= 12) {
                    interval2 -= 12;
                }
                const float ratio1 = std::pow(2.0f, interval1 / 12.0f);
                const float ratio2 = std::pow(2.0f, interval2 / 12.0f);

                const int grain = 4096;
                const int bufFrames = grain * 2;
                if (fx.bufA.size() != bufFrames) {
                    fx.bufA.assign(bufFrames, 0.0f);
                    fx.indexA = 0;
                    fx.readPosA = 0.0f;
                    fx.readPosB = 0.0f;
                    fx.readPosC = 0.0f;
                    fx.readPosD = 0.0f;
                    fx.phaseA = 0.0f;
                    fx.phaseB = 0.5f;
                    fx.phaseC = 0.0f;
                    fx.phaseD = 0.5f;
                    fx.grainSize = grain;
                    fx.bufB.assign(grain, 0.0f);
                    for (int i = 0; i < grain; ++i) {
                        const float t = static_cast<float>(i) / static_cast<float>(grain - 1);
                        fx.bufB[static_cast<size_t>(i)] =
                            0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
                    }
                }

                auto wrapPos = [&](float &pos) {
                    while (pos < 0.0f) {
                        pos += bufFrames;
                    }
                    while (pos >= bufFrames) {
                        pos -= bufFrames;
                    }
                };
                auto readInterp = [&](float pos, int ch) {
                    const int i0 = static_cast<int>(pos);
                    const int i1 = (i0 + 1) % bufFrames;
                    const float frac = pos - i0;
                    const float s0 = fx.bufA[i0];
                    const float s1 = fx.bufA[i1];
                    return s0 + (s1 - s0) * frac;
                };

                const float phaseInc = 1.0f / static_cast<float>(grain);

                for (int i = 0; i < frames; ++i) {
                    const int writePos = fx.indexA;
                    const float inL = buffer[i * m_channels];
                    const float inR = (m_channels > 1) ? buffer[i * m_channels + 1] : inL;
                    const float mono = 0.5f * (inL + inR);
                    fx.bufA[writePos] = mono;

                    const int wiA = qBound(0, static_cast<int>(fx.phaseA * (grain - 1)), grain - 1);
                    const int wiB = qBound(0, static_cast<int>(fx.phaseB * (grain - 1)), grain - 1);
                    const int wiC = qBound(0, static_cast<int>(fx.phaseC * (grain - 1)), grain - 1);
                    const int wiD = qBound(0, static_cast<int>(fx.phaseD * (grain - 1)), grain - 1);
                    const float wA = fx.bufB[static_cast<size_t>(wiA)];
                    const float wB = fx.bufB[static_cast<size_t>(wiB)];
                    const float wC = fx.bufB[static_cast<size_t>(wiC)];
                    const float wD = fx.bufB[static_cast<size_t>(wiD)];

                    const float v1 = readInterp(fx.readPosA, 0) * wA +
                                     readInterp(fx.readPosB, 0) * wB;
                    const float v2 = readInterp(fx.readPosC, 0) * wC +
                                     readInterp(fx.readPosD, 0) * wD;
                    const float add = (v1 + v2) * 0.5f * mix;
                    buffer[i * m_channels] += add;
                    if (m_channels > 1) {
                        buffer[i * m_channels + 1] += add;
                    }

                    fx.readPosA += ratio1;
                    fx.readPosB += ratio1;
                    fx.readPosC += ratio2;
                    fx.readPosD += ratio2;
                    wrapPos(fx.readPosA);
                    wrapPos(fx.readPosB);
                    wrapPos(fx.readPosC);
                    wrapPos(fx.readPosD);

                    fx.phaseA += phaseInc;
                    fx.phaseB += phaseInc;
                    fx.phaseC += phaseInc;
                    fx.phaseD += phaseInc;
                    if (fx.phaseA >= 1.0f) {
                        fx.phaseA -= 1.0f;
                        fx.readPosA = static_cast<float>(writePos - grain);
                        wrapPos(fx.readPosA);
                    }
                    if (fx.phaseB >= 1.0f) {
                        fx.phaseB -= 1.0f;
                        fx.readPosB = static_cast<float>(writePos - grain / 2);
                        wrapPos(fx.readPosB);
                    }
                    if (fx.phaseC >= 1.0f) {
                        fx.phaseC -= 1.0f;
                        fx.readPosC = static_cast<float>(writePos - grain);
                        wrapPos(fx.readPosC);
                    }
                    if (fx.phaseD >= 1.0f) {
                        fx.phaseD -= 1.0f;
                        fx.readPosD = static_cast<float>(writePos - grain / 2);
                        wrapPos(fx.readPosD);
                    }

                    fx.indexA = (fx.indexA + 1) % bufFrames;
                }
                break;
            }
            case 16: {  // pad freeze
                const float lenSec = 0.15f + p1 * 0.85f;
                const float mix = p2;
                const bool refresh = (p3 >= 0.5f);
                const int length = std::max(1, static_cast<int>(lenSec * m_sampleRate));
                const int needed = length * m_channels;
                if (fx.bufA.size() != needed) {
                    fx.bufA.assign(needed, 0.0f);
                    fx.indexA = 0;
                    fx.indexB = 0;
                    fx.env = 0.0f;
                }
                const int framesLen = fx.bufA.size() / m_channels;
                for (int i = 0; i < frames; ++i) {
                    float inL = buffer[i * m_channels];
                    float inR = (m_channels > 1) ? buffer[i * m_channels + 1] : inL;
                    if (fx.env < 1.0f || refresh) {
                        fx.bufA[fx.indexB * m_channels] = inL;
                        if (m_channels > 1) {
                            fx.bufA[fx.indexB * m_channels + 1] = inR;
                        }
                        fx.indexB = (fx.indexB + 1) % framesLen;
                        fx.env = std::min(1.0f, fx.env + 1.0f / framesLen);
                        if (refresh) {
                            fx.indexA = fx.indexB;
                        }
                    }
                    const float frL = fx.bufA[fx.indexA * m_channels];
                    const float frR =
                        (m_channels > 1) ? fx.bufA[fx.indexA * m_channels + 1] : frL;
                    fx.indexA = (fx.indexA + 1) % framesLen;
                    buffer[i * m_channels] = inL * (1.0f - mix) + frL * mix;
                    if (m_channels > 1) {
                        buffer[i * m_channels + 1] = inR * (1.0f - mix) + frR * mix;
                    }
                }
                break;
            }
            default:
                break;
        }
    }
}
