#include "PadBank.h"

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

float clamp01(float value) {
    return qBound(0.0f, value, 1.0f);
}

double pitchToRate(float semitones) {
    return std::pow(2.0, static_cast<double>(semitones) / 12.0);
}

bool isNear(double value, double target) {
    return qAbs(value - target) < 0.001;
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
}  // namespace

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
    QString sourcePath;
    QString effectPath;
    bool effectReady = false;
};

PadBank::PadBank(QObject *parent) : QObject(parent) {
    m_paths.fill(QString());
    bool forceExternal = false;
#ifdef Q_OS_LINUX
    const QString platform = QGuiApplication::platformName();
    if (platform.contains("linuxfb") || platform.contains("eglfs") ||
        platform.contains("vkkhrdisplay")) {
        forceExternal = true;
    }
    if (qEnvironmentVariableIsSet("GROOVEBOX_FORCE_ALSA")) {
        forceExternal = true;
    }
#endif

    for (int i = 0; i < kPadCount; ++i) {
        m_runtime[i] = new PadRuntime();
        m_runtime[i]->useExternal = forceExternal;
        m_runtime[i]->effect = new QSoundEffect(this);
        m_runtime[i]->effect->setLoopCount(1);
        m_runtime[i]->effect->setVolume(1.0f);

        const int index = i;
        connect(m_runtime[i]->effect, &QSoundEffect::statusChanged, this, [this, index]() {
            PadRuntime *rt = m_runtime[index];
            if (!rt || !rt->effect) {
                return;
            }
            rt->effectReady = (rt->effect->status() == QSoundEffect::Ready);
        });

        if (!forceExternal) {
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
    m_params[static_cast<size_t>(index)].start = 0.0f;
    m_params[static_cast<size_t>(index)].end = 1.0f;
    m_params[static_cast<size_t>(index)].sliceIndex = 0;

    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
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
    const QString path = padPath(index);
    if (path.isEmpty()) {
        return QString();
    }
    const int slash = path.lastIndexOf('/');
    return slash >= 0 ? path.mid(slash + 1) : path;
}

bool PadBank::isLoaded(int index) const {
    return !padPath(index).isEmpty();
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
    emit padParamsChanged(index);
}

void PadBank::setStretchIndex(int index, int stretchIndex) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const int maxIndex = stretchCount() - 1;
    m_params[static_cast<size_t>(index)].stretchIndex = qBound(0, stretchIndex, maxIndex);
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
    emit padParamsChanged(index);
}

void PadBank::setSliceIndex(int index, int sliceIndex) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const int count = sliceCountForIndex(m_params[static_cast<size_t>(index)].sliceCountIndex);
    m_params[static_cast<size_t>(index)].sliceIndex = qBound(0, sliceIndex, count - 1);
    emit padParamsChanged(index);
}

void PadBank::setLoop(int index, bool loop) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].loop = loop;
    emit padParamsChanged(index);
}

bool PadBank::isPlaying(int index) const {
    if (index < 0 || index >= padCount()) {
        return false;
    }
    const PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt->external) {
        return rt->external->state() == QProcess::Running;
    }
    return rt->player && rt->player->playbackState() == QMediaPlayer::PlayingState;
}

static bool buildExternalCommand(const QString &path, qint64 startMs, qint64 durationMs,
                                 const QString &filter, bool preferFfplay, QString &program,
                                 QStringList &args) {
#ifdef Q_OS_LINUX
    const QFileInfo info(path);
    const QString ext = info.suffix().toLower();

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
            args = {"-q", path};
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

    const bool stretchEnabled = params.stretchIndex > 0;
    const bool needsSlice = (params.sliceCountIndex > 0) || !isNear(params.start, 0.0) ||
                            !isNear(params.end, 1.0);
    const bool needsEffectTransform =
        stretchEnabled || !isNear(params.pitch, 0.0) || !isNear(params.pan, 0.0);

    if (rt->effect && !rt->effectPath.isEmpty() && !needsSlice && !needsEffectTransform) {
        rt->effect->setLoopCount(params.loop ? QSoundEffect::Infinite : 1);
        rt->effect->setVolume(params.volume);
        rt->effect->play();
        return;
    }

    if (rt->durationMs == 0 && rt->useExternal && (needsSlice || stretchEnabled)) {
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

    const double pitchRate = pitchToRate(params.pitch);
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
                                        !isNear(params.pan, 0.0) || !isNear(params.volume, 1.0);
    const QString filter =
        needsTransformExternal ? buildAudioFilter(params, tempoFactor, pitchRate) : QString();
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
    emit bpmChanged(m_bpm);
}

int PadBank::stretchCount() {
    return static_cast<int>(sizeof(kStretchLabels) / sizeof(kStretchLabels[0]));
}

QString PadBank::stretchLabel(int index) {
    const int maxIndex = stretchCount() - 1;
    const int idx = qBound(0, index, maxIndex);
    return QString::fromLatin1(kStretchLabels[idx]);
}

int PadBank::sliceCountForIndex(int index) {
    const int idx = qBound(0, index, 3);
    return kSliceCounts[idx];
}
