#include "PadBank.h"

#include "AudioEngine.h"

#include <QAudioOutput>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QProcess>
#include <QSoundEffect>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>
#include <QtMath>
#include <cmath>
#include <cstdint>
#include <mutex>

#ifdef GROOVEBOX_WITH_FLUIDSYNTH
#include <fluidsynth.h>
#endif

namespace {
constexpr int kPadCount = 8;
constexpr int kSliceCounts[] = {1, 4, 8, 16};
constexpr const char *kStretchLabels[] = {
    "OFF",
    "1 BEAT",
    "2 BEAT",
    "1 BAR",
    "2 BAR",
    "4 BAR",
    "8 BAR",
};
constexpr const char *kFxBusLabels[] = {
    "MASTER",
    "A",
    "B",
    "C",
    "D",
    "E",
};

struct SynthPresetInfo {
    const char *name;
    int bank;
    int program;
    int note;
};

constexpr SynthPresetInfo kSynthPresets[] = {
    {"PIANO", 0, 0, 60},
    {"E.PIANO", 0, 4, 60},
    {"ORGAN", 0, 16, 60},
    {"BASS", 0, 32, 36},
    {"LEAD", 0, 80, 72},
    {"PAD", 0, 88, 60},
    {"STRINGS", 0, 48, 60},
    {"BRASS", 0, 56, 60},
};

float clamp01(float value) {
    return qBound(0.0f, value, 1.0f);
}

double pitchToRate(float semitones) {
    return std::pow(2.0, static_cast<double>(semitones) / 12.0);
}

bool isNear(double value, double target) {
    return qAbs(value - target) < 0.001;
}

int toMilli(float value) {
    return qBound(0, static_cast<int>(qRound(value * 1000.0f)), 1000);
}

QStringList buildAtempoFilters(double factor) {
    QStringList filters;
    if (isNear(factor, 1.0)) {
        return filters;
    }

    factor = qBound(0.125, factor, 8.0);
    while (factor > 2.0) {
        filters << "atempo=2.0";
        factor /= 2.0;
    }
    while (factor < 0.5) {
        filters << "atempo=0.5";
        factor /= 0.5;
    }
    if (!isNear(factor, 1.0)) {
        filters << QString("atempo=%1").arg(factor, 0, 'f', 3);
    }
    return filters;
}

QString buildAudioFilter(const PadBank::PadParams &params, double tempoFactor, double pitchRate) {
    QStringList filters;

    if (!isNear(params.volume, 1.0)) {
        filters << QString("volume=%1").arg(params.volume, 0, 'f', 3);
    }

    if (!isNear(params.pan, 0.0)) {
        const double left = params.pan <= 0.0 ? 1.0 : 1.0 - params.pan;
        const double right = params.pan >= 0.0 ? 1.0 : 1.0 + params.pan;
        filters << QString("pan=stereo|c0=%1*c0|c1=%2*c1")
                       .arg(left, 0, 'f', 3)
                       .arg(right, 0, 'f', 3);
    }

    const bool pitchActive = !isNear(pitchRate, 1.0);
    if (pitchActive) {
        filters << QString("asetrate=sample_rate*%1").arg(pitchRate, 0, 'f', 4);
        filters << "aresample=sample_rate";
    }

    double atempoFactor = tempoFactor;
    if (pitchActive) {
        atempoFactor = tempoFactor / pitchRate;
    }
    filters.append(buildAtempoFilters(atempoFactor));

    return filters.join(',');
}

QString buildRenderFilter(double tempoFactor, double pitchRate) {
    QStringList filters;
    const bool pitchActive = !isNear(pitchRate, 1.0);
    if (pitchActive) {
        filters << QString("asetrate=sample_rate*%1").arg(pitchRate, 0, 'f', 4);
        filters << "aresample=sample_rate";
    }

    double atempoFactor = tempoFactor;
    if (pitchActive) {
        atempoFactor = tempoFactor / pitchRate;
    }
    filters.append(buildAtempoFilters(atempoFactor));
    return filters.join(',');
}

const SynthPresetInfo *findSynthPreset(const QString &name) {
    const QString key = name.trimmed().toUpper();
    for (const auto &preset : kSynthPresets) {
        if (key == QString::fromLatin1(preset.name)) {
            return &preset;
        }
    }
    return &kSynthPresets[0];
}

#ifdef GROOVEBOX_WITH_FLUIDSYNTH
struct FluidContext {
    fluid_settings_t *settings = nullptr;
    fluid_synth_t *synth = nullptr;
    int sfid = -1;
    int sampleRate = 0;
};

FluidContext *getFluidContext(int sampleRate) {
    static std::mutex mutex;
    static std::unique_ptr<FluidContext> context;
    std::lock_guard<std::mutex> lock(mutex);

    if (context && context->synth && context->sfid >= 0 &&
        context->sampleRate == sampleRate) {
        return context.get();
    }

    if (context && context->synth) {
        delete_fluid_synth(context->synth);
    }
    if (context && context->settings) {
        delete_fluid_settings(context->settings);
    }
    context = std::make_unique<FluidContext>();
    context->sampleRate = sampleRate;
    context->settings = new_fluid_settings();
    fluid_settings_setnum(context->settings, "synth.sample-rate",
                          static_cast<double>(sampleRate));
    fluid_settings_setint(context->settings, "synth.threadsafe-api", 0);
    fluid_settings_setint(context->settings, "synth.chorus.active", 0);
    fluid_settings_setint(context->settings, "synth.reverb.active", 0);

    context->synth = new_fluid_synth(context->settings);
    if (!context->synth) {
        return nullptr;
    }

    QString sf2 = qEnvironmentVariable("GROOVEBOX_SF2");
    QStringList candidates;
    if (!sf2.isEmpty()) {
        candidates << sf2;
    }
    candidates << "/usr/share/sounds/sf2/FluidR3_GM.sf2"
               << "/usr/share/sounds/sf2/TimGM6mb.sf2"
               << "/usr/share/sounds/sf2/FluidR3_GS.sf2";

    QString chosen;
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            chosen = path;
            break;
        }
    }

    if (chosen.isEmpty()) {
        delete_fluid_synth(context->synth);
        delete_fluid_settings(context->settings);
        context.reset();
        return nullptr;
    }

    context->sfid = fluid_synth_sfload(context->synth, chosen.toLocal8Bit().constData(), 1);
    if (context->sfid < 0) {
        delete_fluid_synth(context->synth);
        delete_fluid_settings(context->settings);
        context.reset();
        return nullptr;
    }
    return context.get();
}

std::shared_ptr<AudioEngine::Buffer> renderFluidSynth(const SynthPresetInfo &preset,
                                                      int sampleRate) {
    FluidContext *ctx = getFluidContext(sampleRate);
    if (!ctx || !ctx->synth || ctx->sfid < 0) {
        return nullptr;
    }

    const int totalFrames = sampleRate * 2;
    const int noteFrames = static_cast<int>(sampleRate * 0.85f);
    std::vector<float> left(totalFrames, 0.0f);
    std::vector<float> right(totalFrames, 0.0f);

    fluid_synth_all_notes_off(ctx->synth, 0);
    fluid_synth_program_select(ctx->synth, 0, ctx->sfid, preset.bank, preset.program);
    fluid_synth_noteon(ctx->synth, 0, preset.note, 96);

    fluid_synth_write_float(ctx->synth, noteFrames,
                            left.data(), 0, 1, right.data(), 0, 1);
    fluid_synth_noteoff(ctx->synth, 0, preset.note);
    fluid_synth_write_float(ctx->synth, totalFrames - noteFrames,
                            left.data() + noteFrames, 0, 1,
                            right.data() + noteFrames, 0, 1);

    auto buffer = std::make_shared<AudioEngine::Buffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->samples.resize(totalFrames * 2);

    const int fadeFrames = std::min(sampleRate / 200, totalFrames / 2);
    for (int i = 0; i < totalFrames; ++i) {
        float gain = 1.0f;
        if (i < fadeFrames) {
            gain = static_cast<float>(i) / static_cast<float>(fadeFrames);
        } else if (i > totalFrames - fadeFrames) {
            gain = static_cast<float>(totalFrames - i) / static_cast<float>(fadeFrames);
        }
        buffer->samples[i * 2] = left[i] * gain;
        buffer->samples[i * 2 + 1] = right[i] * gain;
    }
    return buffer;
}
#endif
}  // namespace

struct RenderSignature {
    QString path;
    int pitchCents = 0;
    int stretchIndex = 0;
    int bpm = 120;
    int startMilli = 0;
    int endMilli = 1000;
    int sliceCountIndex = 0;
    int sliceIndex = 0;

    bool operator==(const RenderSignature &other) const {
        return path == other.path && pitchCents == other.pitchCents &&
               stretchIndex == other.stretchIndex && bpm == other.bpm &&
               startMilli == other.startMilli && endMilli == other.endMilli &&
               sliceCountIndex == other.sliceCountIndex && sliceIndex == other.sliceIndex;
    }
};

static RenderSignature makeSignature(const QString &path, const PadBank::PadParams &params, int bpm);

struct PadBank::PadRuntime {
    QMediaPlayer *player = nullptr;
    QAudioOutput *output = nullptr;
    QSoundEffect *effect = nullptr;
    QProcess *external = nullptr;
    QTimer *externalLoop = nullptr;
    qint64 durationMs = 0;
    qint64 segmentStartMs = 0;
    qint64 segmentEndMs = 0;
    bool loop = false;
    bool useExternal = false;
    bool useEngine = false;
    QString sourcePath;
    QString effectPath;
    bool effectReady = false;

    std::shared_ptr<AudioEngine::Buffer> rawBuffer;
    std::shared_ptr<AudioEngine::Buffer> processedBuffer;
    QString rawPath;
    qint64 rawDurationMs = 0;

    QProcess *renderProcess = nullptr;
    QByteArray renderBytes;
    bool renderProcessed = false;
    RenderSignature renderSignature;
    RenderSignature processedSignature;
    bool processedReady = false;
    bool pendingProcessed = false;
    int renderJobId = 0;
    bool pendingTrigger = false;
    float normalizeGain = 1.0f;
};

PadBank::PadBank(QObject *parent) : QObject(parent) {
    m_paths.fill(QString());
    m_engine = std::make_unique<AudioEngine>(this);
    m_engineAvailable = m_engine && m_engine->isAvailable();
    if (m_engineAvailable) {
        m_engineRate = m_engine->sampleRate();
    }
    m_busGain.fill(1.0f);
    if (m_engineAvailable && m_engine) {
        for (int i = 0; i < static_cast<int>(m_busGain.size()); ++i) {
            m_engine->setBusGain(i, m_busGain[static_cast<size_t>(i)]);
        }
    }
    m_ffmpegPath = QStandardPaths::findExecutable("ffmpeg");

    bool forceExternal = false;
#ifdef Q_OS_LINUX
    const QString platform = QGuiApplication::platformName();
    if (!m_engineAvailable &&
        (platform.contains("linuxfb") || platform.contains("eglfs") ||
         platform.contains("vkkhrdisplay"))) {
        forceExternal = true;
    }
    if (!m_engineAvailable && qEnvironmentVariableIsSet("GROOVEBOX_FORCE_ALSA")) {
        forceExternal = true;
    }
#endif

    for (int i = 0; i < kPadCount; ++i) {
        m_runtime[i] = new PadRuntime();
        m_isSynth[static_cast<size_t>(i)] = false;
        m_synthNames[static_cast<size_t>(i)].clear();
        m_runtime[i]->useExternal = forceExternal;
        m_runtime[i]->useEngine = m_engineAvailable;
        if (!m_engineAvailable) {
            m_runtime[i]->effect = new QSoundEffect(this);
            m_runtime[i]->effect->setLoopCount(1);
            m_runtime[i]->effect->setVolume(1.0f);
        }

        const int index = i;
        if (m_runtime[i]->effect) {
            connect(m_runtime[i]->effect, &QSoundEffect::statusChanged, this, [this, index]() {
                PadRuntime *rt = m_runtime[index];
                if (!rt || !rt->effect) {
                    return;
                }
                rt->effectReady = (rt->effect->status() == QSoundEffect::Ready);
            });
        }

        if (!forceExternal && !m_engineAvailable) {
            m_runtime[i]->output = new QAudioOutput(this);
            m_runtime[i]->player = new QMediaPlayer(this);
            m_runtime[i]->player->setAudioOutput(m_runtime[i]->output);

            connect(m_runtime[i]->player, &QMediaPlayer::durationChanged, this,
                    [this, index](qint64 duration) {
                        m_runtime[index]->durationMs = duration;
                    });
            connect(m_runtime[i]->player, &QMediaPlayer::errorOccurred, this,
                    [this, index](QMediaPlayer::Error error, const QString &) {
                        if (error == QMediaPlayer::NoError) {
                            return;
                        }
                        m_runtime[index]->useExternal = true;
                    });
            connect(m_runtime[i]->player, &QMediaPlayer::positionChanged, this,
                    [this, index](qint64 position) {
                        PadRuntime *rt = m_runtime[index];
                        if (rt->segmentEndMs <= rt->segmentStartMs) {
                            return;
                        }
                        if (position >= rt->segmentEndMs) {
                            if (rt->loop) {
                                rt->player->setPosition(rt->segmentStartMs);
                            } else {
                                rt->player->stop();
                            }
                        }
                    });
        }
    }
}

PadBank::~PadBank() {
    for (int i = 0; i < kPadCount; ++i) {
        PadRuntime *rt = m_runtime[i];
        if (!rt) {
            continue;
        }
        if (rt->external) {
            rt->external->kill();
            rt->external->deleteLater();
        }
        if (rt->renderProcess) {
            rt->renderProcess->kill();
            rt->renderProcess->deleteLater();
        }
        delete rt;
        m_runtime[i] = nullptr;
    }
}

void PadBank::setActivePad(int index) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (m_activePad == index) {
        return;
    }
    m_activePad = index;
    emit activePadChanged(index);
}

void PadBank::setPadPath(int index, const QString &path) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (m_paths[static_cast<size_t>(index)] == path) {
        return;
    }
    m_paths[static_cast<size_t>(index)] = path;
    m_isSynth[static_cast<size_t>(index)] = false;
    m_synthNames[static_cast<size_t>(index)].clear();
    m_params[static_cast<size_t>(index)].start = 0.0f;
    m_params[static_cast<size_t>(index)].end = 1.0f;
    m_params[static_cast<size_t>(index)].sliceIndex = 0;

    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt) {
        rt->rawBuffer.reset();
        rt->processedBuffer.reset();
        rt->processedReady = false;
        rt->pendingProcessed = false;
        rt->rawPath.clear();
        rt->rawDurationMs = 0;
        rt->normalizeGain = 1.0f;
    }
    if (rt && rt->player) {
        rt->player->setSource(QUrl::fromLocalFile(path));
        rt->sourcePath = path;
    }
    if (rt && rt->effect) {
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == "wav") {
            if (rt->effectPath != path) {
                rt->effect->stop();
                rt->effect->setSource(QUrl::fromLocalFile(path));
                rt->effectPath = path;
                rt->effectReady = false;
            }
        } else {
            rt->effect->stop();
            rt->effect->setSource(QUrl());
            rt->effectPath.clear();
            rt->effectReady = false;
        }
    }
    scheduleRawRender(index);
    emit padChanged(index);
    emit padParamsChanged(index);
}

QString PadBank::padPath(int index) const {
    if (index < 0 || index >= padCount()) {
        return QString();
    }
    return m_paths[static_cast<size_t>(index)];
}

QString PadBank::padName(int index) const {
    if (isSynth(index)) {
        const QString name = m_synthNames[static_cast<size_t>(index)];
        return name.isEmpty() ? QString("SYNTH") : name;
    }
    const QString path = padPath(index);
    if (path.isEmpty()) {
        return QString();
    }
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.mid(slash + 1) : path;
}

bool PadBank::isLoaded(int index) const {
    return !padPath(index).isEmpty() || isSynth(index);
}

bool PadBank::isSynth(int index) const {
    if (index < 0 || index >= padCount()) {
        return false;
    }
    return m_isSynth[static_cast<size_t>(index)];
}

QString PadBank::synthName(int index) const {
    if (!isSynth(index)) {
        return QString();
    }
    return m_synthNames[static_cast<size_t>(index)];
}

static std::shared_ptr<AudioEngine::Buffer> buildSynthBuffer(const QString &name, int sampleRate) {
#ifdef GROOVEBOX_WITH_FLUIDSYNTH
    const SynthPresetInfo *preset = findSynthPreset(name);
    if (preset) {
        auto rendered = renderFluidSynth(*preset, sampleRate);
        if (rendered && rendered->isValid()) {
            return rendered;
        }
    }
#endif

    const int frames = sampleRate;
    auto buffer = std::make_shared<AudioEngine::Buffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->samples.resize(frames * 2);

    const QString wave = name.toLower();
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float v = 0.0f;
        if (wave.contains("saw")) {
            v = 2.0f * (t - std::floor(t + 0.5f));
        } else if (wave.contains("square")) {
            v = (std::sin(2.0f * static_cast<float>(M_PI) * 220.0f * t) >= 0.0f) ? 0.7f : -0.7f;
        } else {
            v = std::sin(2.0f * static_cast<float>(M_PI) * 220.0f * t);
        }
        buffer->samples[i * 2] = v;
        buffer->samples[i * 2 + 1] = v;
    }
    return buffer;
}

void PadBank::setSynth(int index, const QString &name) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_isSynth[static_cast<size_t>(index)] = true;
    m_synthNames[static_cast<size_t>(index)] = name;
    m_paths[static_cast<size_t>(index)].clear();

    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt) {
        rt->rawBuffer = buildSynthBuffer(name, m_engineRate);
        rt->processedBuffer = rt->rawBuffer;
        rt->processedReady = true;
        rt->pendingProcessed = false;
        rt->rawPath = QString("synth:%1").arg(name);
        if (rt->rawBuffer && rt->rawBuffer->isValid()) {
            rt->rawDurationMs =
                static_cast<qint64>((rt->rawBuffer->frames() * 1000) / rt->rawBuffer->sampleRate);
        } else {
            rt->rawDurationMs = 1000;
        }
        rt->durationMs = rt->rawDurationMs;
        rt->normalizeGain = 1.0f;
    }
    emit padChanged(index);
    emit padParamsChanged(index);
}

PadBank::PadParams PadBank::params(int index) const {
    if (index < 0 || index >= padCount()) {
        return PadParams();
    }
    return m_params[static_cast<size_t>(index)];
}

void PadBank::setVolume(int index, float value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].volume = clamp01(value);
    emit padParamsChanged(index);
}

void PadBank::setPan(int index, float value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].pan = qBound(-1.0f, value, 1.0f);
    emit padParamsChanged(index);
}

void PadBank::setPitch(int index, float semitones) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].pitch = qBound(-12.0f, semitones, 12.0f);
    if (!m_engineAvailable) {
        scheduleProcessedRender(index);
    }
    emit padParamsChanged(index);
}

void PadBank::setStretchIndex(int index, int stretchIndex) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const int maxIndex = stretchCount() - 1;
    m_params[static_cast<size_t>(index)].stretchIndex = qBound(0, stretchIndex, maxIndex);
    if (!m_engineAvailable) {
        scheduleProcessedRender(index);
    }
    emit padParamsChanged(index);
}

void PadBank::setStart(int index, float value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    value = clamp01(value);
    float end = m_params[static_cast<size_t>(index)].end;
    if (value >= end) {
        value = qMax(0.0f, end - 0.01f);
    }
    m_params[static_cast<size_t>(index)].start = value;
    if (!m_engineAvailable) {
        scheduleProcessedRender(index);
    }
    emit padParamsChanged(index);
}

void PadBank::setEnd(int index, float value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    value = clamp01(value);
    float start = m_params[static_cast<size_t>(index)].start;
    if (value <= start) {
        value = qMin(1.0f, start + 0.01f);
    }
    m_params[static_cast<size_t>(index)].end = value;
    if (!m_engineAvailable) {
        scheduleProcessedRender(index);
    }
    emit padParamsChanged(index);
}

void PadBank::setSliceCountIndex(int index, int sliceCountIndex) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const int clamped = qBound(0, sliceCountIndex, 3);
    m_params[static_cast<size_t>(index)].sliceCountIndex = clamped;

    const int count = sliceCountForIndex(clamped);
    int &sliceIndex = m_params[static_cast<size_t>(index)].sliceIndex;
    sliceIndex = qBound(0, sliceIndex, count - 1);
    if (!m_engineAvailable) {
        scheduleProcessedRender(index);
    }
    emit padParamsChanged(index);
}

void PadBank::setSliceIndex(int index, int sliceIndex) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const int count = sliceCountForIndex(m_params[static_cast<size_t>(index)].sliceCountIndex);
    m_params[static_cast<size_t>(index)].sliceIndex = qBound(0, sliceIndex, count - 1);
    if (!m_engineAvailable) {
        scheduleProcessedRender(index);
    }
    emit padParamsChanged(index);
}

void PadBank::setLoop(int index, bool loop) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].loop = loop;
    emit padParamsChanged(index);
}

void PadBank::setNormalize(int index, bool enabled) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].normalize = enabled;
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt && rt->rawBuffer && rt->rawBuffer->isValid()) {
        float peak = 0.0f;
        for (float v : rt->rawBuffer->samples) {
            const float a = std::fabs(v);
            if (a > peak) {
                peak = a;
            }
        }
        if (peak > 0.0001f) {
            rt->normalizeGain = qBound(0.5f, 1.0f / peak, 2.5f);
        } else {
            rt->normalizeGain = 1.0f;
        }
    }
    emit padParamsChanged(index);
}

bool PadBank::isPlaying(int index) const {
    if (index < 0 || index >= padCount()) {
        return false;
    }
    const PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt && rt->useEngine && m_engineAvailable && m_engine) {
        return m_engine->isPadActive(index);
    }
    if (rt->external) {
        return rt->external->state() == QProcess::Running;
    }
    return rt->player && rt->player->playbackState() == QMediaPlayer::PlayingState;
}

bool PadBank::isPadReady(int index) const {
    if (index < 0 || index >= padCount()) {
        return true;
    }
    if (!m_engineAvailable || !m_engine) {
        return true;
    }
    const PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return true;
    }
    if (padPath(index).isEmpty()) {
        return false;
    }
    if (!rt->rawBuffer || !rt->rawBuffer->isValid()) {
        return false;
    }
    const PadParams params = m_params[static_cast<size_t>(index)];
    if (needsProcessing(params)) {
        const RenderSignature sig = makeSignature(padPath(index), params, m_bpm);
        if (!rt->processedReady || !(sig == rt->processedSignature)) {
            return false;
        }
    }
    return true;
}

static bool buildExternalCommand(const QString &path, qint64 startMs, qint64 durationMs,
                                 const QString &filter, bool preferFfplay, QString &program,
                                 QStringList &args) {
#ifdef Q_OS_LINUX
    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();
    const QString alsaDevice = qEnvironmentVariable("GROOVEBOX_ALSA_DEVICE");

    const QString ffplay = QStandardPaths::findExecutable("ffplay");
    if (!ffplay.isEmpty() && (preferFfplay || !filter.isEmpty())) {
        program = ffplay;
        args = {"-nodisp", "-autoexit", "-loglevel", "quiet"};
        if (startMs > 0) {
            args << "-ss" << QString::number(static_cast<double>(startMs) / 1000.0, 'f', 3);
        }
        if (durationMs > 0) {
            args << "-t" << QString::number(static_cast<double>(durationMs) / 1000.0, 'f', 3);
        }
        if (!filter.isEmpty()) {
            args << "-af" << filter;
        }
        args << path;
        return true;
    }

    if (!filter.isEmpty() || preferFfplay) {
        return false;
    }

    if (ext == "wav") {
        program = QStandardPaths::findExecutable("aplay");
        if (program.isEmpty()) {
            return false;
        }
        args = {"-q"};
        if (!alsaDevice.isEmpty()) {
            args << "-D" << alsaDevice;
        }
        if (durationMs > 0) {
            const int seconds = static_cast<int>(qCeil(durationMs / 1000.0));
            args << "-d" << QString::number(qMax(1, seconds));
        }
        args << path;
        return true;
    }

    if (ext == "mp3") {
        program = QStandardPaths::findExecutable("mpg123");
        if (!program.isEmpty()) {
            args = {"-q"};
            if (!alsaDevice.isEmpty()) {
                args << "-a" << alsaDevice;
            }
            args << path;
            return true;
        }
    }
#else
    Q_UNUSED(path);
    Q_UNUSED(startMs);
    Q_UNUSED(durationMs);
    Q_UNUSED(program);
    Q_UNUSED(args);
#endif
    return false;
}

static qint64 stretchTargetMs(int bpm, int stretchIndex) {
    const int beatMs = 60000 / qMax(1, bpm);
    switch (stretchIndex) {
        case 1:
            return beatMs;
        case 2:
            return beatMs * 2;
        case 3:
            return beatMs * 4;
        case 4:
            return beatMs * 8;
        case 5:
            return beatMs * 16;
        case 6:
            return beatMs * 32;
        default:
            return 0;
    }
}

static qint64 probeDurationMs(const QString &path) {
#ifdef Q_OS_LINUX
    const QString ffprobe = QStandardPaths::findExecutable("ffprobe");
    if (ffprobe.isEmpty()) {
        return 0;
    }

    QProcess proc;
    proc.setProgram(ffprobe);
    proc.setArguments({"-v", "error",
                       "-show_entries", "format=duration",
                       "-of", "default=noprint_wrappers=1:nokey=1",
                       path});
    proc.start();
    if (!proc.waitForFinished(700)) {
        proc.kill();
        return 0;
    }

    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    bool ok = false;
    const double seconds = out.toDouble(&ok);
    if (!ok || seconds <= 0.0) {
        return 0;
    }
    return static_cast<qint64>(seconds * 1000.0);
#else
    Q_UNUSED(path);
    return 0;
#endif
}

static RenderSignature makeSignature(const QString &path, const PadBank::PadParams &params, int bpm) {
    RenderSignature sig;
    sig.path = path;
    sig.pitchCents = qRound(params.pitch * 100.0f);
    sig.stretchIndex = params.stretchIndex;
    sig.bpm = bpm;
    if (params.stretchIndex > 0) {
        sig.startMilli = toMilli(params.start);
        sig.endMilli = toMilli(params.end);
        sig.sliceCountIndex = params.sliceCountIndex;
        sig.sliceIndex = params.sliceIndex;
    }
    return sig;
}

static std::shared_ptr<AudioEngine::Buffer> decodePcm16(const QByteArray &bytes, int sampleRate,
                                                        int channels) {
    if (bytes.isEmpty() || channels <= 0 || sampleRate <= 0) {
        return nullptr;
    }
    const int sampleCount = bytes.size() / static_cast<int>(sizeof(int16_t));
    if (sampleCount <= 0) {
        return nullptr;
    }
    auto buffer = std::make_shared<AudioEngine::Buffer>();
    buffer->channels = channels;
    buffer->sampleRate = sampleRate;
    buffer->samples.resize(sampleCount);
    const int16_t *src = reinterpret_cast<const int16_t *>(bytes.constData());
    for (int i = 0; i < sampleCount; ++i) {
        buffer->samples[i] = static_cast<float>(src[i]) / 32768.0f;
    }
    return buffer;
}

static QStringList buildFfmpegArgs(const QString &path, const QString &filter, int sampleRate,
                                   int channels) {
    QStringList args = {"-v", "error", "-i", path, "-vn"};
    if (!filter.isEmpty()) {
        args << "-af" << filter;
    }
    args << "-ac" << QString::number(qMax(1, channels));
    args << "-ar" << QString::number(qMax(8000, sampleRate));
    args << "-f" << "s16le" << "-";
    return args;
}

static QStringList buildFfmpegArgsSegment(const QString &path, const QString &filter, int sampleRate,
                                          int channels, qint64 startMs, qint64 durationMs) {
    QStringList args = {"-v", "error"};
    if (startMs > 0) {
        args << "-ss" << QString::number(static_cast<double>(startMs) / 1000.0, 'f', 3);
    }
    args << "-i" << path << "-vn";
    if (durationMs > 0) {
        args << "-t" << QString::number(static_cast<double>(durationMs) / 1000.0, 'f', 3);
    }
    if (!filter.isEmpty()) {
        args << "-af" << filter;
    }
    args << "-ac" << QString::number(qMax(1, channels));
    args << "-ar" << QString::number(qMax(8000, sampleRate));
    args << "-f" << "s16le" << "-";
    return args;
}

bool PadBank::needsProcessing(const PadParams &params) const {
    if (m_engineAvailable) {
        return false;
    }
    return params.stretchIndex > 0;
}

void PadBank::scheduleRawRender(int index) {
    if (!m_engineAvailable) {
        return;
    }
    if (index < 0 || index >= padCount()) {
        return;
    }
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return;
    }
    const QString path = padPath(index);
    if (path.isEmpty()) {
        return;
    }
    if (rt->rawPath == path && rt->rawBuffer && rt->rawBuffer->isValid()) {
        if (needsProcessing(m_params[static_cast<size_t>(index)])) {
            scheduleProcessedRender(index);
        }
        return;
    }
    if (m_ffmpegPath.isEmpty()) {
        return;
    }

    if (rt->renderProcess) {
        rt->renderProcess->kill();
        rt->renderProcess->deleteLater();
        rt->renderProcess = nullptr;
    }

    rt->renderBytes.clear();
    rt->renderProcessed = false;
    rt->renderSignature = RenderSignature();
    rt->renderSignature.path = path;
    rt->pendingProcessed = false;
    const int jobId = ++m_renderSerial;
    rt->renderJobId = jobId;

    QProcess *proc = new QProcess(this);
    rt->renderProcess = proc;
    proc->setProgram(m_ffmpegPath);
    proc->setArguments(buildFfmpegArgs(path, QString(), m_engineRate, 2));
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, index, jobId]() {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        if (!rt || rt->renderJobId != jobId || !rt->renderProcess) {
            return;
        }
        rt->renderBytes.append(rt->renderProcess->readAllStandardOutput());
    });
    connect(proc, &QProcess::errorOccurred, this, [this, index, jobId](QProcess::ProcessError) {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        if (!rt || rt->renderJobId != jobId) {
            return;
        }
        if (rt->renderProcess) {
            rt->renderProcess->deleteLater();
            rt->renderProcess = nullptr;
        }
    });
    connect(proc,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this, index, jobId](int, QProcess::ExitStatus) {
                PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
                if (!rt || rt->renderJobId != jobId) {
                    return;
                }
                if (rt->renderProcess) {
                    rt->renderProcess->deleteLater();
                    rt->renderProcess = nullptr;
                }
                const QByteArray bytes = rt->renderBytes;
                rt->renderBytes.clear();
                auto buffer = decodePcm16(bytes, m_engineRate, 2);
                if (buffer && buffer->isValid()) {
                    rt->rawBuffer = buffer;
                    rt->rawPath = rt->renderSignature.path;
                    rt->rawDurationMs =
                        (buffer->frames() * 1000LL) / qMax(1, buffer->sampleRate);
                    rt->durationMs = rt->rawDurationMs;
                    float peak = 0.0f;
                    for (float v : buffer->samples) {
                        const float a = std::fabs(v);
                        if (a > peak) {
                            peak = a;
                        }
                    }
                    if (peak > 0.0001f) {
                        rt->normalizeGain = qBound(0.5f, 1.0f / peak, 2.5f);
                    } else {
                        rt->normalizeGain = 1.0f;
                    }
                }
                if (needsProcessing(m_params[static_cast<size_t>(index)])) {
                    scheduleProcessedRender(index);
                } else if (rt->pendingTrigger) {
                    rt->pendingTrigger = false;
                    triggerPad(index);
                }
            });

    proc->start();
}

void PadBank::scheduleProcessedRender(int index) {
    if (!m_engineAvailable) {
        return;
    }
    if (index < 0 || index >= padCount()) {
        return;
    }
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return;
    }

    const QString path = padPath(index);
    if (path.isEmpty()) {
        return;
    }
    const PadParams params = m_params[static_cast<size_t>(index)];
    if (!needsProcessing(params)) {
        rt->processedBuffer.reset();
        rt->processedReady = false;
        return;
    }
    if (m_ffmpegPath.isEmpty()) {
        return;
    }

    if (!rt->rawBuffer || !rt->rawBuffer->isValid() || rt->rawPath != path) {
        rt->pendingProcessed = true;
        scheduleRawRender(index);
        return;
    }

    const RenderSignature sig = makeSignature(path, params, m_bpm);
    if (rt->processedReady && sig == rt->processedSignature) {
        return;
    }

    if (rt->renderProcess) {
        rt->renderProcess->kill();
        rt->renderProcess->deleteLater();
        rt->renderProcess = nullptr;
    }

    double tempoFactor = 1.0;
    qint64 renderStartMs = 0;
    qint64 renderDurationMs = 0;
    if (params.stretchIndex > 0 && rt->rawDurationMs > 0) {
        float start = clamp01(params.start);
        float end = clamp01(params.end);
        if (end <= start) {
            end = qMin(1.0f, start + 0.01f);
        }
        const int sliceCount = sliceCountForIndex(params.sliceCountIndex);
        const int sliceIndex = qBound(0, params.sliceIndex, sliceCount - 1);
        const float sliceLen = (end - start) / static_cast<float>(sliceCount);
        const float sliceStart = start + sliceLen * sliceIndex;
        const float sliceEnd = sliceStart + sliceLen;
        renderStartMs = static_cast<qint64>(static_cast<double>(rt->rawDurationMs) * sliceStart);
        renderDurationMs =
            static_cast<qint64>(static_cast<double>(rt->rawDurationMs) * (sliceEnd - sliceStart));
        if (renderDurationMs <= 0) {
            renderDurationMs = 1;
        }
        const double segmentMs = static_cast<double>(renderDurationMs);
        const qint64 targetMs = stretchTargetMs(m_bpm, params.stretchIndex);
        if (targetMs > 0 && segmentMs > 0.0) {
            tempoFactor = segmentMs / static_cast<double>(targetMs);
        }
    }
    tempoFactor = qBound(0.25, tempoFactor, 4.0);

    const double pitchRate = pitchToRate(params.pitch);
    const QString filter = buildRenderFilter(tempoFactor, pitchRate);
    if (filter.isEmpty()) {
        rt->processedBuffer = rt->rawBuffer;
        rt->processedSignature = sig;
        rt->processedReady = true;
        return;
    }

    rt->renderBytes.clear();
    rt->renderProcessed = true;
    rt->renderSignature = sig;
    rt->pendingProcessed = false;
    rt->processedReady = false;
    const int jobId = ++m_renderSerial;
    rt->renderJobId = jobId;

    QProcess *proc = new QProcess(this);
    rt->renderProcess = proc;
    proc->setProgram(m_ffmpegPath);
    proc->setArguments(
        buildFfmpegArgsSegment(path, filter, m_engineRate, 2, renderStartMs, renderDurationMs));
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, index, jobId]() {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        if (!rt || rt->renderJobId != jobId || !rt->renderProcess) {
            return;
        }
        rt->renderBytes.append(rt->renderProcess->readAllStandardOutput());
    });
    connect(proc, &QProcess::errorOccurred, this, [this, index, jobId](QProcess::ProcessError) {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        if (!rt || rt->renderJobId != jobId) {
            return;
        }
        if (rt->renderProcess) {
            rt->renderProcess->deleteLater();
            rt->renderProcess = nullptr;
        }
    });
    connect(proc,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this, index, jobId](int, QProcess::ExitStatus) {
                PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
                if (!rt || rt->renderJobId != jobId) {
                    return;
                }
                if (rt->renderProcess) {
                    rt->renderProcess->deleteLater();
                    rt->renderProcess = nullptr;
                }
                const QByteArray bytes = rt->renderBytes;
                rt->renderBytes.clear();
                auto buffer = decodePcm16(bytes, m_engineRate, 2);
                if (buffer && buffer->isValid()) {
                    rt->processedBuffer = buffer;
                    rt->processedSignature = rt->renderSignature;
                    rt->processedReady = true;
                }
                if (rt->pendingTrigger) {
                    rt->pendingTrigger = false;
                    triggerPad(index);
                }
            });

    proc->start();
}

void PadBank::triggerPad(int index) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const QString path = padPath(index);
    if (path.isEmpty()) {
        return;
    }

    PadParams &params = m_params[static_cast<size_t>(index)];
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return;
    }

    const double pitchRate = pitchToRate(params.pitch);
    const bool wantsProcessing = needsProcessing(params);
    const bool stretchEnabled = params.stretchIndex > 0;
    const float normalizeGain = (params.normalize && rt) ? rt->normalizeGain : 1.0f;
    if (rt->useEngine && m_engineAvailable && m_engine) {
        std::shared_ptr<AudioEngine::Buffer> buffer;
        if (wantsProcessing) {
            const RenderSignature sig = makeSignature(path, params, m_bpm);
            if (rt->processedReady && sig == rt->processedSignature) {
                buffer = rt->processedBuffer;
            } else {
                rt->pendingTrigger = true;
                scheduleProcessedRender(index);
                return;
            }
        } else {
            buffer = rt->rawBuffer;
        }
        if (!buffer || !buffer->isValid()) {
            rt->pendingTrigger = true;
            scheduleRawRender(index);
            return;
        }
        if (buffer && buffer->isValid()) {
            float start = clamp01(params.start);
            float end = clamp01(params.end);
            if (end <= start) {
                end = qMin(1.0f, start + 0.01f);
            }

            const int sliceCount = sliceCountForIndex(params.sliceCountIndex);
            const int sliceIndex = qBound(0, params.sliceIndex, sliceCount - 1);
            const float sliceLen = (end - start) / static_cast<float>(sliceCount);
            const float sliceStart = start + sliceLen * sliceIndex;
            const float sliceEnd = sliceStart + sliceLen;

            const int totalFrames = buffer->frames();
            int startFrame = static_cast<int>(sliceStart * totalFrames);
            int endFrame = static_cast<int>(sliceEnd * totalFrames);
            if (endFrame <= startFrame) {
                endFrame = qMin(totalFrames, startFrame + 1);
            }

            if (wantsProcessing) {
                startFrame = 0;
                endFrame = totalFrames;
            }
            float tempoFactor = 1.0f;
            if (stretchEnabled) {
                const int segmentFrames = qMax(1, endFrame - startFrame);
                const qint64 segmentMs =
                    static_cast<qint64>(segmentFrames * 1000.0 / qMax(1, buffer->sampleRate));
                const qint64 targetMs = stretchTargetMs(m_bpm, params.stretchIndex);
                if (targetMs > 0) {
                    tempoFactor = static_cast<float>(
                        static_cast<double>(segmentMs) / static_cast<double>(targetMs));
                }
                tempoFactor = qBound(0.25f, tempoFactor, 4.0f);
            }

            const float rate = static_cast<float>(pitchRate) * tempoFactor;
            const float volume = params.volume * normalizeGain;
            m_engine->trigger(index, buffer, startFrame, endFrame, params.loop, volume,
                              params.pan, rate, params.fxBus);
            rt->pendingTrigger = false;
            return;
        }
    }

    const bool needsSlice = (params.sliceCountIndex > 0) || !isNear(params.start, 0.0) ||
                            !isNear(params.end, 1.0);
    const bool needsEffectTransform =
        stretchEnabled || !isNear(params.pitch, 0.0) || !isNear(params.pan, 0.0) ||
        (params.normalize && !isNear(normalizeGain, 1.0f));

    if (rt->effect && !rt->effectPath.isEmpty() && !needsSlice && !needsEffectTransform) {
        if (rt->effect->status() == QSoundEffect::Ready) {
            if (rt->effect->isPlaying()) {
                rt->effect->stop();
            }
            rt->effect->setLoopCount(params.loop ? QSoundEffect::Infinite : 1);
            rt->effect->setVolume(params.volume * normalizeGain);
            rt->effect->play();
            return;
        }
    }

    if (rt->durationMs == 0 && (rt->useExternal || !rt->player) &&
        (needsSlice || stretchEnabled)) {
        rt->durationMs = probeDurationMs(path);
    }

    float start = clamp01(params.start);
    float end = clamp01(params.end);
    if (end <= start) {
        end = qMin(1.0f, start + 0.01f);
    }

    const int sliceCount = sliceCountForIndex(params.sliceCountIndex);
    const int sliceIndex = qBound(0, params.sliceIndex, sliceCount - 1);
    const float sliceLen = (end - start) / static_cast<float>(sliceCount);
    const float sliceStart = start + sliceLen * sliceIndex;
    const float sliceEnd = sliceStart + sliceLen;

    const qint64 durationMs = rt->durationMs;
    qint64 startMs = durationMs > 0 ? static_cast<qint64>(sliceStart * durationMs) : 0;
    qint64 endMs = durationMs > 0 ? static_cast<qint64>(sliceEnd * durationMs) : 0;
    if (durationMs > 0 && endMs <= startMs) {
        endMs = qMin(durationMs, startMs + 5);
    }

    rt->segmentStartMs = startMs;
    rt->segmentEndMs = endMs;
    rt->loop = params.loop;

    const qint64 segmentMs = (durationMs > 0) ? qMax<qint64>(1, endMs - startMs) : 0;
    double tempoFactor = 1.0;
    if (stretchEnabled && segmentMs > 0) {
        const qint64 targetMs = stretchTargetMs(m_bpm, params.stretchIndex);
        if (targetMs > 0) {
            tempoFactor = static_cast<double>(segmentMs) / static_cast<double>(targetMs);
        }
    }
    tempoFactor = qBound(0.25, tempoFactor, 4.0);

    const double internalRate = qBound(0.25, tempoFactor * pitchRate, 4.0);

    const bool wantsStretch = !isNear(tempoFactor, 1.0);
    const bool needsExternal = rt->useExternal || wantsStretch;

    if (!needsExternal && rt->player) {
        if (rt->sourcePath != path) {
            rt->player->setSource(QUrl::fromLocalFile(path));
            rt->sourcePath = path;
        }
        rt->output->setVolume(params.volume);
        rt->player->setPlaybackRate(internalRate);
        if (durationMs > 0 && startMs > 0) {
            rt->player->setPosition(startMs);
        } else {
            rt->player->setPosition(0);
        }
        rt->player->play();
        return;
    }

    const bool needsTransformExternal = wantsStretch || !isNear(params.pitch, 0.0) ||
                                        !isNear(params.pan, 0.0) ||
                                        !isNear(params.volume * normalizeGain, 1.0f);
    PadParams renderParams = params;
    renderParams.volume = params.volume * normalizeGain;
    const QString filter =
        needsTransformExternal ? buildAudioFilter(renderParams, tempoFactor, pitchRate) : QString();
    QString program;
    QStringList args;
    if (!buildExternalCommand(path, startMs, segmentMs, filter, needsSlice || needsTransformExternal, program,
                              args)) {
        if (!filter.isEmpty() &&
            buildExternalCommand(path, startMs, segmentMs, QString(), false, program, args)) {
            // Fallback without filters if ffplay is missing.
        } else if (rt->player) {
            rt->output->setVolume(params.volume);
            rt->player->setPlaybackRate(internalRate);
            rt->player->setPosition(startMs);
            rt->player->play();
            return;
        } else {
            return;
        }
    }

    if (rt->external) {
        rt->external->kill();
        rt->external->deleteLater();
        rt->external = nullptr;
    }

    rt->external = new QProcess(this);
    rt->external->setProgram(program);
    rt->external->setArguments(args);
    rt->external->setProcessChannelMode(QProcess::MergedChannels);

    connect(rt->external,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this, index](int, QProcess::ExitStatus) {
                PadRuntime *loopRt = m_runtime[static_cast<size_t>(index)];
                if (!loopRt) {
                    return;
                }
                if (loopRt->loop) {
                    QTimer::singleShot(10, this, [this, index]() { triggerPad(index); });
                }
            });

    rt->external->start();
}

void PadBank::stopPad(int index) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return;
    }
    rt->pendingTrigger = false;
    if (rt->useEngine && m_engineAvailable && m_engine) {
        m_engine->stopPad(index);
    }
    if (rt->effect) {
        rt->effect->stop();
    }
    if (rt->player) {
        rt->player->stop();
    }
    if (rt->external) {
        rt->external->kill();
        rt->external->deleteLater();
        rt->external = nullptr;
    }
}

void PadBank::stopAll() {
    if (m_engineAvailable && m_engine) {
        m_engine->stopAll();
    }
    for (int i = 0; i < padCount(); ++i) {
        stopPad(i);
    }
}

void PadBank::setBpm(int bpm) {
    const int next = qBound(30, bpm, 300);
    if (m_bpm == next) {
        return;
    }
    m_bpm = next;
    for (int i = 0; i < padCount(); ++i) {
        if (m_params[static_cast<size_t>(i)].stretchIndex > 0) {
            scheduleProcessedRender(i);
        }
    }
    emit bpmChanged(m_bpm);
}

void PadBank::setBusEffects(int bus, const QVector<BusEffect> &effects) {
    if (!m_engineAvailable || !m_engine) {
        return;
    }
    if (bus < 0 || bus >= 6) {
        return;
    }
    std::vector<AudioEngine::EffectSettings> settings;
    settings.reserve(effects.size());
    for (const BusEffect &fx : effects) {
        AudioEngine::EffectSettings cfg;
        cfg.type = fx.type;
        cfg.p1 = fx.p1;
        cfg.p2 = fx.p2;
        cfg.p3 = fx.p3;
        cfg.p4 = fx.p4;
        cfg.p5 = fx.p5;
        settings.push_back(cfg);
    }
    m_engine->setBusEffects(bus, settings);
}

float PadBank::busMeter(int bus) const {
    if (!m_engineAvailable || !m_engine) {
        return 0.0f;
    }
    return m_engine->busMeter(bus);
}

float PadBank::busGain(int bus) const {
    if (bus < 0 || bus >= static_cast<int>(m_busGain.size())) {
        return 1.0f;
    }
    return m_busGain[static_cast<size_t>(bus)];
}

void PadBank::setBusGain(int bus, float gain) {
    if (bus < 0 || bus >= static_cast<int>(m_busGain.size())) {
        return;
    }
    const float clamped = qBound(0.0f, gain, 1.2f);
    m_busGain[static_cast<size_t>(bus)] = clamped;
    if (m_engineAvailable && m_engine) {
        m_engine->setBusGain(bus, clamped);
    }
}

float PadBank::normalizeGainForPad(int index) const {
    if (index < 0 || index >= padCount()) {
        return 1.0f;
    }
    const PadParams &params = m_params[static_cast<size_t>(index)];
    if (!params.normalize) {
        return 1.0f;
    }
    const PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return 1.0f;
    }
    return rt->normalizeGain;
}

int PadBank::stretchCount() {
    return static_cast<int>(sizeof(kStretchLabels) / sizeof(kStretchLabels[0]));
}

QString PadBank::stretchLabel(int index) {
    const int maxIndex = stretchCount() - 1;
    const int idx = qBound(0, index, maxIndex);
    return QString::fromLatin1(kStretchLabels[idx]);
}

QStringList PadBank::synthPresets() {
    QStringList list;
    for (const auto &preset : kSynthPresets) {
        list << QString::fromLatin1(preset.name);
    }
#ifdef GROOVEBOX_WITH_FLUIDSYNTH
    return list;
#else
    list.clear();
    list << "SINE" << "SAW" << "SQUARE";
    return list;
#endif
}

bool PadBank::hasFluidSynth() {
#ifdef GROOVEBOX_WITH_FLUIDSYNTH
    return true;
#else
    return false;
#endif
}

int PadBank::sliceCountForIndex(int index) {
    const int idx = qBound(0, index, 3);
    return kSliceCounts[idx];
}

int PadBank::fxBus(int index) const {
    if (index < 0 || index >= padCount()) {
        return 0;
    }
    return m_params[static_cast<size_t>(index)].fxBus;
}

void PadBank::setFxBus(int index, int bus) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const int maxIndex = static_cast<int>(sizeof(kFxBusLabels) / sizeof(kFxBusLabels[0])) - 1;
    const int next = qBound(0, bus, maxIndex);
    m_params[static_cast<size_t>(index)].fxBus = next;
    emit padParamsChanged(index);
}

QString PadBank::fxBusLabel(int index) {
    const int maxIndex = static_cast<int>(sizeof(kFxBusLabels) / sizeof(kFxBusLabels[0])) - 1;
    const int idx = qBound(0, index, maxIndex);
    return QString::fromLatin1(kFxBusLabels[idx]);
}
