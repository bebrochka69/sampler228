#include "PadBank.h"

#include "AudioEngine.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QProcess>
#include <QFile>
#include <QSoundEffect>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QSet>
#include <QtGlobal>
#include <QtMath>
#include <QDirIterator>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "dx7_core.h"

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

QString defaultMiniDexedType();

QString synthTypeFromName(const QString &name) {
    const QString upper = name.trimmed().toUpper();
    if (upper.startsWith("SERUM")) {
        return "SERUM";
    }
    if (upper.startsWith("FM")) {
        return "SERUM";
    }
    if (upper.startsWith("DX7")) {
        return "DX7";
    }
    if (upper.startsWith("MINIDEXED") || upper.startsWith("MINI DEXED")) {
        return "DX7";
    }
    if (upper.contains(":")) {
        const int colon = upper.indexOf(':');
        return upper.left(colon).trimmed();
    }
    return QStringLiteral("SERUM");
}

QString synthPresetFromName(const QString &name) {
    QString value = name.trimmed();
    const int colon = value.indexOf(':');
    if (colon >= 0) {
        value = value.mid(colon + 1);
    }
    return value.trimmed();
}

QString makeSynthName(const QString &type, const QString &preset) {
    const QString t = type.trimmed().toUpper();
    return QString("%1:%2").arg(t, preset);
}

bool isMiniDexedType(const QString &type) {
    const QString t = type.trimmed().toUpper();
    return t == "DX7";
}

bool isFmType(const QString &type) {
    const QString t = type.trimmed().toUpper();
    return t == "FM" || t == "SERUM";
}

QString defaultMiniDexedType() {
    return QStringLiteral("DX7");
}

float clamp01(float value) {
    return qBound(0.0f, value, 1.0f);
}

struct Dx7Bank {
    QString name;
    QString path;
    QStringList programs;
};

static QVector<Dx7Bank> g_dx7Banks;
static bool g_dx7BanksScanned = false;
static QStringList internalProgramNames();

QString makeProgramLabel(int index) {
    return QString("PROGRAM %1").arg(index + 1, 2, 10, QChar('0'));
}

static void scanDx7Banks() {
    if (g_dx7BanksScanned) {
        return;
    }
    g_dx7BanksScanned = true;
    g_dx7Banks.clear();

    QSet<QString> files;
    QStringList roots;
    roots << QDir::currentPath();
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!roots.contains(appDir)) {
        roots << appDir;
    }

    QStringList searchDirs;
    for (const QString &root : roots) {
        QDir dir(root);
        searchDirs << dir.filePath("sysex")
                   << dir.filePath("assets/sysex")
                   << dir.filePath("assets/dx7")
                   << dir.filePath("data/sysex")
                   << dir.filePath("MiniDexed-main/Synth_Dexed/tools/sysex");
    }

    for (const QString &dir : searchDirs) {
        QDirIterator it(dir, {"*.syx", "*.SYX"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            files.insert(QDir::cleanPath(it.next()));
        }
    }

    QHash<QString, int> nameCounts;
    for (const QString &path : files) {
        Dx7Core core;
        if (!core.loadSysexFile(path.toStdString())) {
            continue;
        }
        const int count = core.programCount();
        if (count <= 0) {
            continue;
        }

        Dx7Bank bank;
        const QFileInfo info(path);
        QString baseName = info.completeBaseName().trimmed();
        if (baseName.isEmpty()) {
            baseName = "BANK";
        }
        const QString key = baseName.toUpper();
        const int dup = nameCounts.value(key, 0);
        nameCounts[key] = dup + 1;
        bank.name = (dup > 0) ? QString("%1 (%2)").arg(baseName).arg(dup + 1) : baseName;
        bank.path = path;
        bank.programs.reserve(count);
        for (int i = 0; i < count; ++i) {
            QString program = QString::fromUtf8(core.programName(i)).trimmed();
            if (program.isEmpty()) {
                program = makeProgramLabel(i);
            }
            bank.programs << program;
        }
        g_dx7Banks.push_back(bank);
    }

    std::sort(g_dx7Banks.begin(), g_dx7Banks.end(),
              [](const Dx7Bank &a, const Dx7Bank &b) {
                  return a.name.toLower() < b.name.toLower();
              });

    if (g_dx7Banks.isEmpty()) {
        Dx7Bank bank;
        bank.name = "INTERNAL";
        bank.path.clear();
        bank.programs = internalProgramNames();
        g_dx7Banks.push_back(bank);
    }
}

static const Dx7Bank &defaultDx7Bank() {
    scanDx7Banks();
    return g_dx7Banks.front();
}

static int bankIndexForName(const QString &name) {
    scanDx7Banks();
    if (g_dx7Banks.isEmpty()) {
        return -1;
    }
    if (name.trimmed().isEmpty()) {
        return 0;
    }
    for (int i = 0; i < g_dx7Banks.size(); ++i) {
        if (QString::compare(g_dx7Banks[i].name, name, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return 0;
}

static int programIndexForName(const Dx7Bank &bank, const QString &token) {
    if (bank.programs.isEmpty()) {
        return 0;
    }
    const QString trimmed = token.trimmed();
    if (!trimmed.isEmpty()) {
        for (int i = 0; i < bank.programs.size(); ++i) {
            if (QString::compare(bank.programs[i], trimmed, Qt::CaseInsensitive) == 0) {
                return i;
            }
        }
        QString digits;
        for (QChar ch : trimmed) {
            if (ch.isDigit()) {
                digits.append(ch);
            }
        }
        if (!digits.isEmpty()) {
            const int n = digits.toInt();
            if (n > 0 && n <= bank.programs.size()) {
                return n - 1;
            }
        }
    }
    return 0;
}

static QStringList internalProgramNames() {
    return {"INIT", "PIANO 1", "E.PIANO"};
}

struct FmPreset {
    QString name;
    PadBank::SynthParams params;
};

static QVector<FmPreset> g_fmPresets;
static bool g_fmPresetsReady = false;

static void ensureFmPresets() {
    if (g_fmPresetsReady) {
        return;
    }
    g_fmPresetsReady = true;
    g_fmPresets.clear();

    auto base = PadBank::SynthParams();

    FmPreset init;
    init.name = "INIT";
    init.params = base;
    init.params.fmAmount = 0.0f;
    init.params.ratio = 1.0f;
    init.params.feedback = 0.0f;
    init.params.osc1Wave = 0;
    init.params.osc2Wave = 0;
    init.params.osc1Voices = 1;
    init.params.osc2Voices = 1;
    init.params.osc1Detune = 0.0f;
    init.params.osc2Detune = 0.0f;
    init.params.osc1Gain = 0.8f;
    init.params.osc2Gain = 0.5f;
    init.params.osc1Pan = -0.1f;
    init.params.osc2Pan = 0.1f;
    init.params.filterType = 0;
    init.params.cutoff = 0.9f;
    init.params.resonance = 0.1f;
    init.params.attack = 0.05f;
    init.params.decay = 0.2f;
    init.params.sustain = 0.8f;
    init.params.release = 0.2f;
    g_fmPresets.push_back(init);

    FmPreset piano;
    piano.name = "FM PIANO";
    piano.params = base;
    piano.params.fmAmount = 0.55f;
    piano.params.ratio = 2.0f;
    piano.params.feedback = 0.25f;
    piano.params.osc1Wave = 0;
    piano.params.osc2Wave = 0;
    piano.params.osc1Gain = 0.8f;
    piano.params.osc2Gain = 0.6f;
    piano.params.filterType = 0;
    piano.params.cutoff = 0.85f;
    piano.params.resonance = 0.15f;
    piano.params.attack = 0.02f;
    piano.params.decay = 0.3f;
    piano.params.sustain = 0.6f;
    piano.params.release = 0.25f;
    g_fmPresets.push_back(piano);

    FmPreset bell;
    bell.name = "FM BELL";
    bell.params = base;
    bell.params.fmAmount = 0.8f;
    bell.params.ratio = 3.0f;
    bell.params.feedback = 0.4f;
    bell.params.osc1Wave = 7;
    bell.params.osc2Wave = 0;
    bell.params.osc1Gain = 0.75f;
    bell.params.osc2Gain = 0.6f;
    bell.params.filterType = 0;
    bell.params.cutoff = 0.95f;
    bell.params.resonance = 0.1f;
    bell.params.attack = 0.01f;
    bell.params.decay = 0.25f;
    bell.params.sustain = 0.3f;
    bell.params.release = 0.35f;
    g_fmPresets.push_back(bell);

    FmPreset bass;
    bass.name = "FM BASS";
    bass.params = base;
    bass.params.fmAmount = 0.35f;
    bass.params.ratio = 1.0f;
    bass.params.feedback = 0.15f;
    bass.params.osc1Wave = 1;
    bass.params.osc2Wave = 2;
    bass.params.osc1Gain = 0.9f;
    bass.params.osc2Gain = 0.4f;
    bass.params.filterType = 0;
    bass.params.cutoff = 0.45f;
    bass.params.resonance = 0.25f;
    bass.params.attack = 0.01f;
    bass.params.decay = 0.25f;
    bass.params.sustain = 0.7f;
    bass.params.release = 0.15f;
    g_fmPresets.push_back(bass);

    FmPreset pad;
    pad.name = "FM PAD";
    pad.params = base;
    pad.params.fmAmount = 0.4f;
    pad.params.ratio = 1.5f;
    pad.params.feedback = 0.1f;
    pad.params.osc1Wave = 6;
    pad.params.osc2Wave = 0;
    pad.params.osc1Voices = 2;
    pad.params.osc2Voices = 1;
    pad.params.osc1Detune = 0.2f;
    pad.params.osc1Gain = 0.8f;
    pad.params.osc2Gain = 0.4f;
    pad.params.filterType = 0;
    pad.params.cutoff = 0.6f;
    pad.params.resonance = 0.2f;
    pad.params.attack = 0.3f;
    pad.params.decay = 0.4f;
    pad.params.sustain = 0.8f;
    pad.params.release = 0.5f;
    g_fmPresets.push_back(pad);
}

static const FmPreset *findFmPreset(const QString &name) {
    ensureFmPresets();
    for (const auto &preset : g_fmPresets) {
        if (QString::compare(preset.name, name, Qt::CaseInsensitive) == 0) {
            return &preset;
        }
    }
    return g_fmPresets.isEmpty() ? nullptr : &g_fmPresets.front();
}

static QStringList fmPresetNames() {
    ensureFmPresets();
    QStringList list;
    for (const auto &preset : g_fmPresets) {
        list << preset.name;
    }
    return list;
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

}  // namespace

static AudioEngine::FmParams buildFmParams(const PadBank::SynthParams &sp);

struct RenderSignature {
    QString path;
    int pitchCents = 0;
    int stretchIndex = 0;
    int stretchMode = 0;
    int bpm = 120;
    int startMilli = 0;
    int endMilli = 1000;
    int sliceCountIndex = 0;
    int sliceIndex = 0;

    bool operator==(const RenderSignature &other) const {
        return path == other.path && pitchCents == other.pitchCents &&
               stretchIndex == other.stretchIndex && stretchMode == other.stretchMode &&
               bpm == other.bpm &&
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
    QByteArray renderInput;
    bool renderProcessed = false;
    RenderSignature renderSignature;
    int renderSampleRate = 0;
    RenderSignature processedSignature;
    bool processedReady = false;
    bool pendingProcessed = false;
    int renderJobId = 0;
    bool pendingTrigger = false;
    float normalizeGain = 1.0f;
    int synthStopToken = 0;
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
        m_engine->setBpm(m_bpm);
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
    if (forceExternal && !m_engineAvailable) {
        const QString ffplay = QStandardPaths::findExecutable("ffplay");
        const QString aplay = QStandardPaths::findExecutable("aplay");
        if (ffplay.isEmpty() && aplay.isEmpty()) {
            forceExternal = false;
        }
    }

    int defaultVoices = 8;
    {
        bool ok = false;
        const int envVoices = qEnvironmentVariableIntValue("GROOVEBOX_DX7_VOICES", &ok);
        if (ok) {
            defaultVoices = qBound(1, envVoices, 8);
        }
    }

    for (int i = 0; i < kPadCount; ++i) {
        m_runtime[i] = new PadRuntime();
        m_isSynth[static_cast<size_t>(i)] = false;
        m_synthNames[static_cast<size_t>(i)].clear();
        m_synthBanks[static_cast<size_t>(i)].clear();
        m_synthPrograms[static_cast<size_t>(i)] = 0;
        m_synthBaseMidi[static_cast<size_t>(i)] = 60;
        m_synthParams[static_cast<size_t>(i)].voices = defaultVoices;
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
        if (m_engineAvailable && m_engine) {
            m_engine->setPadAdsr(i, 0.0f, 0.0f, 1.0f, 0.0f);
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
    m_synthBanks[static_cast<size_t>(index)].clear();
    m_synthPrograms[static_cast<size_t>(index)] = 0;
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
    if (m_engineAvailable && m_engine) {
        m_engine->setPadAdsr(index, 0.0f, 0.0f, 1.0f, 0.0f);
        m_engine->setSynthEnabled(index, false);
    }
    emit padChanged(index);
    emit padParamsChanged(index);
}

void PadBank::copyPad(int from, int to) {
    if (from < 0 || from >= padCount() || to < 0 || to >= padCount() || from == to) {
        return;
    }
    const PadParams srcParams = m_params[static_cast<size_t>(from)];
    if (isSynth(from)) {
        const QString synthId = m_synthNames[static_cast<size_t>(from)];
        setSynth(to, synthId);
        m_synthParams[static_cast<size_t>(to)] = m_synthParams[static_cast<size_t>(from)];
        m_synthBaseMidi[static_cast<size_t>(to)] = m_synthBaseMidi[static_cast<size_t>(from)];
        m_params[static_cast<size_t>(to)] = srcParams;
    } else {
        const QString path = m_paths[static_cast<size_t>(from)];
        setPadPath(to, path);
        m_params[static_cast<size_t>(to)] = srcParams;
        if (needsProcessing(srcParams)) {
            scheduleProcessedRender(to);
        } else {
            scheduleRawRender(to);
        }
    }
    emit padChanged(to);
    emit padParamsChanged(to);
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
    const QString raw = m_synthNames[static_cast<size_t>(index)];
    QString preset = synthPresetFromName(raw);
    const int slash = preset.indexOf('/');
    if (slash >= 0) {
        preset = preset.mid(slash + 1).trimmed();
    }
    return preset.isEmpty() ? defaultMiniDexedType() : preset;
}

QString PadBank::synthId(int index) const {
    if (!isSynth(index)) {
        return QString();
    }
    return m_synthNames[static_cast<size_t>(index)];
}

static std::shared_ptr<AudioEngine::Buffer> buildSynthBuffer(const QString &name, int sampleRate,
                                                            int baseMidi, const PadBank::SynthParams &params) {
    const QString preset = synthPresetFromName(name);

    const QStringList waves = {"SINE", "SAW", "SQUARE", "TRI", "NOISE"};
    int waveIndex = qBound(0, params.wave, waves.size() - 1);
    QString waveName = preset.isEmpty() ? waves[waveIndex] : preset;
    const QString wavLower = waveName.toLower();
    if (wavLower.contains("saw")) {
        waveName = "SAW";
    } else if (wavLower.contains("square")) {
        waveName = "SQUARE";
    } else if (wavLower.contains("tri")) {
        waveName = "TRI";
    } else if (wavLower.contains("noise")) {
        waveName = "NOISE";
    } else if (wavLower.contains("sine") || wavLower.contains("piano") || wavLower.contains("keys")) {
        waveName = "SINE";
    }

    const int frames = sampleRate * 2;
    auto buffer = std::make_shared<AudioEngine::Buffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->samples.resize(frames * 2);

    const int voices = qBound(1, params.voices, 8);
    const float detune = qBound(0.0f, params.detune, 0.9f);
    const int octave = qBound(-2, params.octave, 2);
    const float baseFreq =
        440.0f * std::pow(2.0f, (static_cast<float>(baseMidi + octave * 12) - 69.0f) / 12.0f);

    uint32_t noiseSeed = 0x1234567u;
    auto nextNoise = [&noiseSeed]() {
        noiseSeed = 1664525u * noiseSeed + 1013904223u;
        return (static_cast<int>(noiseSeed >> 8) & 0xFFFF) / 32768.0f - 1.0f;
    };

    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
        float sum = 0.0f;
        for (int v = 0; v < voices; ++v) {
            const float det = (static_cast<float>(v) - (voices - 1) * 0.5f) * detune * 0.6f;
            const float freq = baseFreq * std::pow(2.0f, det / 12.0f);
            float vout = 0.0f;
            const QString wav = waveName.toLower();
            if (wav.contains("saw")) {
                const float phase = std::fmod(freq * t, 1.0f);
                vout = 2.0f * (phase - 0.5f);
            } else if (wav.contains("square")) {
                vout = (std::sin(2.0f * static_cast<float>(M_PI) * freq * t) >= 0.0f) ? 0.8f : -0.8f;
            } else if (wav.contains("tri")) {
                const float phase = std::fmod(freq * t, 1.0f);
                vout = 1.0f - 4.0f * std::fabs(phase - 0.5f);
            } else if (wav.contains("noise")) {
                vout = nextNoise() * 0.6f;
            } else {
                vout = std::sin(2.0f * static_cast<float>(M_PI) * freq * t);
            }
            sum += vout;
        }
        float v = sum / static_cast<float>(voices);
        buffer->samples[i * 2] = v;
        buffer->samples[i * 2 + 1] = v;
    }

    return buffer;
}

void PadBank::rebuildSynthRuntime(PadRuntime *rt, const QString &name, int sampleRate,
                                  int baseMidi, const PadBank::SynthParams &params) {
    if (!rt) {
        return;
    }
    rt->rawBuffer = buildSynthBuffer(name, sampleRate, baseMidi, params);
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
}

void PadBank::setSynth(int index, const QString &name) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    QString synthName = name.trimmed();
    QString type = synthTypeFromName(synthName);

    if (isFmType(type)) {
        if (!synthName.contains(":")) {
            synthName = makeSynthName(QStringLiteral("SERUM"), QStringLiteral("INIT"));
        }
        const QString presetToken = synthPresetFromName(synthName);
        QString presetName = presetToken;
        const int slash = presetToken.indexOf('/');
        if (slash >= 0) {
            presetName = presetToken.mid(slash + 1).trimmed();
        }
        const FmPreset *preset = findFmPreset(presetName);
        if (preset) {
            m_synthParams[static_cast<size_t>(index)] = preset->params;
        }

        m_isSynth[static_cast<size_t>(index)] = true;
        const QString typeName = type.trimmed().isEmpty() ? QStringLiteral("SERUM") : type;
        m_synthNames[static_cast<size_t>(index)] = makeSynthName(typeName, presetName);
        m_synthBanks[static_cast<size_t>(index)] = "SERUM";
        m_synthPrograms[static_cast<size_t>(index)] = 0;
        m_paths[static_cast<size_t>(index)].clear();
        m_synthBaseMidi[static_cast<size_t>(index)] = 60;

        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        if (rt) {
            rt->rawBuffer.reset();
            rt->processedBuffer.reset();
            rt->processedReady = false;
            rt->pendingProcessed = false;
            rt->rawPath = QString("synth:%1").arg(synthName);
            rt->rawDurationMs = 0;
            rt->durationMs = 0;
            rt->normalizeGain = 1.0f;
        }
        if (m_engineAvailable && m_engine) {
            const SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
            m_engine->setSynthKind(index, AudioEngine::SynthKind::SimpleFm);
            m_engine->setPadAdsr(index, sp.attack, sp.decay, sp.sustain, sp.release);
            m_engine->setSynthVoices(index, sp.voices);
            m_engine->setFmParams(index, buildFmParams(sp));
            const PadParams &pp = m_params[static_cast<size_t>(index)];
            m_engine->setSynthParams(index, pp.volume, pp.pan, pp.fxBus);
            m_engine->setSynthEnabled(index, true);
        }
        emit padChanged(index);
        emit padParamsChanged(index);
        return;
    }

    const Dx7Bank &defaultBank = defaultDx7Bank();
    const QString fallbackProgram =
        defaultBank.programs.isEmpty() ? makeProgramLabel(0) : defaultBank.programs.first();
    const QString fallbackPreset = QString("%1/%2").arg(defaultBank.name, fallbackProgram);

    if (!synthName.contains(":") || !isMiniDexedType(type)) {
        synthName = makeSynthName(defaultMiniDexedType(), fallbackPreset);
        type = synthTypeFromName(synthName);
    }

    QString presetToken = synthPresetFromName(synthName);
    QString bankToken;
    QString programToken;
    const int slash = presetToken.indexOf('/');
    if (slash >= 0) {
        bankToken = presetToken.left(slash).trimmed();
        programToken = presetToken.mid(slash + 1).trimmed();
    } else {
        programToken = presetToken.trimmed();
    }

    const int bankIndex = bankIndexForName(bankToken);
    const Dx7Bank &bank = (bankIndex >= 0 && bankIndex < g_dx7Banks.size())
                              ? g_dx7Banks[bankIndex]
                              : defaultBank;
    const int programIndex = programIndexForName(bank, programToken);
    QString programName = bank.programs.value(programIndex);
    if (programName.isEmpty()) {
        programName = makeProgramLabel(programIndex);
    }

    const QString resolvedPreset = QString("%1/%2").arg(bank.name, programName);
    synthName = makeSynthName(defaultMiniDexedType(), resolvedPreset);

    m_isSynth[static_cast<size_t>(index)] = true;
    m_synthNames[static_cast<size_t>(index)] = synthName;
    m_synthBanks[static_cast<size_t>(index)] = bank.name;
    m_synthPrograms[static_cast<size_t>(index)] = programIndex;
    m_paths[static_cast<size_t>(index)].clear();
    m_synthBaseMidi[static_cast<size_t>(index)] = 60;
    {
        // Reset external ADSR so DX7 presets sound authentic by default.
        SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
        sp.attack = 0.0f;
        sp.decay = 0.0f;
        sp.sustain = 1.0f;
        sp.release = 0.0f;
    }

    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt) {
        if (isMiniDexedType(type)) {
            rt->rawBuffer.reset();
            rt->processedBuffer.reset();
            rt->processedReady = false;
            rt->pendingProcessed = false;
            rt->rawPath = QString("synth:%1").arg(synthName);
            rt->rawDurationMs = 0;
            rt->durationMs = 0;
            rt->normalizeGain = 1.0f;
        }
    }
    if (m_engineAvailable && m_engine && isMiniDexedType(type)) {
        const SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
        m_engine->setSynthKind(index, AudioEngine::SynthKind::Dx7);
        m_engine->setPadAdsr(index, sp.attack, sp.decay, sp.sustain, sp.release);
        m_engine->setSynthVoices(index, sp.voices);
        if (!bank.path.isEmpty()) {
            m_engine->loadSynthSysex(index, bank.path);
        }
        m_engine->setSynthProgram(index, programIndex);
        const PadParams &pp = m_params[static_cast<size_t>(index)];
        m_engine->setSynthParams(index, pp.volume, pp.pan, pp.fxBus);
        m_engine->setSynthEnabled(index, true);
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

PadBank::SynthParams PadBank::synthParams(int index) const {
    if (index < 0 || index >= padCount()) {
        return SynthParams();
    }
    return m_synthParams[static_cast<size_t>(index)];
}

void PadBank::setVolume(int index, float value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].volume = clamp01(value);
    if (isSynth(index) && m_engineAvailable && m_engine) {
        const PadParams &pp = m_params[static_cast<size_t>(index)];
        m_engine->setSynthParams(index, pp.volume, pp.pan, pp.fxBus);
    }
    emit padParamsChanged(index);
}

void PadBank::setPan(int index, float value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].pan = qBound(-1.0f, value, 1.0f);
    if (isSynth(index) && m_engineAvailable && m_engine) {
        const PadParams &pp = m_params[static_cast<size_t>(index)];
        m_engine->setSynthParams(index, pp.volume, pp.pan, pp.fxBus);
    }
    emit padParamsChanged(index);
}

void PadBank::setPitch(int index, float semitones) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    m_params[static_cast<size_t>(index)].pitch = qBound(-12.0f, semitones, 12.0f);
    if (!m_engineAvailable) {
        scheduleProcessedRender(index);
    } else if (needsProcessing(m_params[static_cast<size_t>(index)])) {
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
    scheduleProcessedRender(index);
    emit padParamsChanged(index);
}

void PadBank::setStretchMode(int index, int mode) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    const int clamped = qBound(0, mode, 1);
    if (m_params[static_cast<size_t>(index)].stretchMode == clamped) {
        return;
    }
    m_params[static_cast<size_t>(index)].stretchMode = clamped;
    scheduleProcessedRender(index);
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
    if (!m_engineAvailable || needsProcessing(m_params[static_cast<size_t>(index)])) {
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
    if (!m_engineAvailable || needsProcessing(m_params[static_cast<size_t>(index)])) {
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
    if (!m_engineAvailable || needsProcessing(m_params[static_cast<size_t>(index)])) {
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
    if (!m_engineAvailable || needsProcessing(m_params[static_cast<size_t>(index)])) {
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

void PadBank::setSynthAdsr(int index, float attack, float decay, float sustain, float release) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.attack = clamp01(attack);
    sp.decay = clamp01(decay);
    sp.sustain = clamp01(sustain);
    sp.release = clamp01(release);
    if (m_engineAvailable && m_engine) {
        m_engine->setPadAdsr(index, sp.attack, sp.decay, sp.sustain, sp.release);
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthWave(int index, int wave) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (isMiniDexedType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.wave = qBound(0, wave, 4);
    if (isSynth(index)) {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        rebuildSynthRuntime(rt, m_synthNames[static_cast<size_t>(index)], m_engineRate,
                            m_synthBaseMidi[static_cast<size_t>(index)], sp);
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthVoices(int index, int voices) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.voices = qBound(1, voices, 8);
    if (isSynth(index) && m_engineAvailable && m_engine) {
        m_engine->setSynthVoices(index, sp.voices);
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthDetune(int index, float detune) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (isMiniDexedType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.detune = qBound(0.0f, detune, 1.0f);
    if (isSynth(index)) {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        rebuildSynthRuntime(rt, m_synthNames[static_cast<size_t>(index)], m_engineRate,
                            m_synthBaseMidi[static_cast<size_t>(index)], sp);
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthOctave(int index, int octave) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (isMiniDexedType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.octave = qBound(-2, octave, 2);
    if (isSynth(index)) {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        rebuildSynthRuntime(rt, m_synthNames[static_cast<size_t>(index)], m_engineRate,
                            m_synthBaseMidi[static_cast<size_t>(index)], sp);
    }
    emit padParamsChanged(index);
}

static AudioEngine::FmParams buildFmParams(const PadBank::SynthParams &sp) {
    AudioEngine::FmParams fm;
    fm.fmAmount = sp.fmAmount;
    fm.ratio = sp.ratio;
    fm.feedback = sp.feedback;
    fm.cutoff = sp.cutoff;
    fm.resonance = sp.resonance;
    fm.filterType = sp.filterType;
    fm.lfoRate = sp.lfoRate;
    fm.lfoDepth = sp.lfoDepth;
    fm.osc1Wave = sp.osc1Wave;
    fm.osc2Wave = sp.osc2Wave;
    fm.osc1Voices = sp.osc1Voices;
    fm.osc2Voices = sp.osc2Voices;
    fm.osc1Detune = sp.osc1Detune;
    fm.osc2Detune = sp.osc2Detune;
    fm.osc1Gain = sp.osc1Gain;
    fm.osc2Gain = sp.osc2Gain;
    fm.osc1Pan = sp.osc1Pan;
    fm.osc2Pan = sp.osc2Pan;
    fm.macros = sp.macros;
    return fm;
}

void PadBank::setSynthFm(int index, float fmAmount, float ratio, float feedback) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.fmAmount = qBound(0.0f, fmAmount, 1.0f);
    sp.ratio = qBound(0.1f, ratio, 8.0f);
    sp.feedback = qBound(0.0f, feedback, 1.0f);
    if (isSynth(index) && m_engineAvailable && m_engine &&
        isFmType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        m_engine->setFmParams(index, buildFmParams(sp));
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthFilter(int index, float cutoff, float resonance) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.cutoff = qBound(0.0f, cutoff, 1.0f);
    sp.resonance = qBound(0.0f, resonance, 1.0f);
    if (isSynth(index) && m_engineAvailable && m_engine &&
        isFmType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        m_engine->setFmParams(index, buildFmParams(sp));
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthFilterType(int index, int type) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.filterType = qBound(0, type, 9);
    if (isSynth(index) && m_engineAvailable && m_engine &&
        isFmType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        m_engine->setFmParams(index, buildFmParams(sp));
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthOsc(int index, int osc, int wave, int voices, float detune, float gain,
                          float pan) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    const int clampedWave = qBound(0, wave, static_cast<int>(serumWaves().size()) - 1);
    const int clampedVoices = qBound(1, voices, 8);
    const float clampedDetune = qBound(0.0f, detune, 1.0f);
    const float clampedGain = qBound(0.0f, gain, 1.0f);
    const float clampedPan = qBound(-1.0f, pan, 1.0f);

    if (osc == 0) {
        sp.osc1Wave = clampedWave;
        sp.osc1Voices = clampedVoices;
        sp.osc1Detune = clampedDetune;
        sp.osc1Gain = clampedGain;
        sp.osc1Pan = clampedPan;
    } else {
        sp.osc2Wave = clampedWave;
        sp.osc2Voices = clampedVoices;
        sp.osc2Detune = clampedDetune;
        sp.osc2Gain = clampedGain;
        sp.osc2Pan = clampedPan;
    }

    if (isSynth(index) && m_engineAvailable && m_engine &&
        isFmType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        m_engine->setFmParams(index, buildFmParams(sp));
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthLfo(int index, float rate, float depth) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.lfoRate = qBound(0.0f, rate, 1.0f);
    sp.lfoDepth = qBound(0.0f, depth, 1.0f);
    if (isSynth(index) && m_engineAvailable && m_engine &&
        isFmType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        m_engine->setFmParams(index, buildFmParams(sp));
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthMacro(int index, int macro, float value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (macro < 0 || macro >= 8) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.macros[static_cast<size_t>(macro)] = qBound(0.0f, value, 1.0f);
    if (isSynth(index) && m_engineAvailable && m_engine &&
        isFmType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        m_engine->setFmParams(index, buildFmParams(sp));
    }
    emit padParamsChanged(index);
}

bool PadBank::isPlaying(int index) const {
    if (index < 0 || index >= padCount()) {
        return false;
    }
    const PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt && rt->useEngine && m_engineAvailable && m_engine) {
        if (isSynth(index)) {
            return m_engine->isSynthActive(index);
        }
        return m_engine->isPadActive(index);
    }
    if (rt->external) {
        return rt->external->state() == QProcess::Running;
    }
    return rt->player && rt->player->playbackState() == QMediaPlayer::PlayingState;
}

float PadBank::padPlayhead(int index) const {
    if (index < 0 || index >= padCount()) {
        return -1.0f;
    }
    const PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt && rt->useEngine && m_engineAvailable && m_engine && !isSynth(index)) {
        const float ph = m_engine->padPlayhead(index);
        if (ph >= 0.0f) {
            return ph;
        }
    }
    if (rt && rt->player && rt->player->playbackState() == QMediaPlayer::PlayingState &&
        rt->durationMs > 0) {
        const qint64 start = rt->segmentStartMs;
        const qint64 end = (rt->segmentEndMs > start) ? rt->segmentEndMs : rt->durationMs;
        const qint64 span = end - start;
        if (span > 0) {
            const qint64 pos = rt->player->position();
            const float ratio =
                static_cast<float>(static_cast<double>(pos - start) / static_cast<double>(span));
            return qBound(0.0f, ratio, 1.0f);
        }
    }
    return -1.0f;
}

std::shared_ptr<AudioEngine::Buffer> PadBank::rawBuffer(int index) const {
    if (index < 0 || index >= padCount()) {
        return nullptr;
    }
    const PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return nullptr;
    }
    return rt->rawBuffer;
}

void PadBank::requestRawBuffer(int index) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    scheduleRawRender(index);
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
    if (isSynth(index)) {
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
    sig.stretchMode = params.stretchMode;
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
    if (durationMs > 0) {
        args << "-t" << QString::number(static_cast<double>(durationMs) / 1000.0, 'f', 3);
    }
    args << "-i" << path << "-vn";
    if (!filter.isEmpty()) {
        args << "-af" << filter;
    }
    args << "-ac" << QString::number(qMax(1, channels));
    args << "-ar" << QString::number(qMax(8000, sampleRate));
    args << "-f" << "s16le" << "-";
    return args;
}

static QStringList buildFfmpegArgsRaw(const QString &filter, int sampleRate, int channels) {
    QStringList args = {"-v", "error",
                        "-f", "f32le",
                        "-ac", QString::number(qMax(1, channels)),
                        "-ar", QString::number(qMax(8000, sampleRate)),
                        "-i", "-",
                        "-vn"};
    if (!filter.isEmpty()) {
        args << "-af" << filter;
    }
    args << "-ac" << QString::number(qMax(1, channels));
    args << "-ar" << QString::number(qMax(8000, sampleRate));
    args << "-f" << "s16le" << "-";
    return args;
}

bool PadBank::needsProcessing(const PadParams &params) const {
    return params.stretchIndex > 0 && params.stretchMode > 0;
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
        if (rt->renderProcess) {
            rt->renderProcess->kill();
            rt->renderProcess->deleteLater();
            rt->renderProcess = nullptr;
        }
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
    rt->renderInput.clear();
    rt->renderProcessed = true;
    rt->renderSignature = sig;
    rt->pendingProcessed = false;
    rt->processedReady = false;
    const int jobId = ++m_renderSerial;
    rt->renderJobId = jobId;

    QProcess *proc = new QProcess(this);
    rt->renderProcess = proc;
    proc->setProgram(m_ffmpegPath);

    bool useRawInput = false;
    int renderSampleRate = m_engineRate;
    if (rt->rawBuffer && rt->rawBuffer->isValid()) {
        const int totalFrames = rt->rawBuffer->frames();
        const int sampleRate = rt->rawBuffer->sampleRate;
        const int channels = rt->rawBuffer->channels;
        int startFrame = 0;
        int endFrame = totalFrames;
        if (renderStartMs > 0) {
            startFrame = static_cast<int>((renderStartMs * sampleRate) / 1000);
        }
        if (renderDurationMs > 0) {
            endFrame = startFrame + static_cast<int>((renderDurationMs * sampleRate) / 1000);
        }
        startFrame = qBound(0, startFrame, totalFrames);
        endFrame = qBound(startFrame + 1, endFrame, totalFrames);
        const int frames = qMax(0, endFrame - startFrame);
        if (frames > 0 && channels > 0) {
            rt->renderInput.resize(frames * channels * static_cast<int>(sizeof(float)));
            float *dst = reinterpret_cast<float *>(rt->renderInput.data());
            const int offset = startFrame * channels;
            const int count = frames * channels;
            for (int i = 0; i < count; ++i) {
                dst[i] = rt->rawBuffer->samples[offset + i];
            }
            proc->setArguments(buildFfmpegArgsRaw(filter, sampleRate, channels));
            renderSampleRate = sampleRate;
            useRawInput = true;
        }
    }
    if (!useRawInput) {
        proc->setArguments(
            buildFfmpegArgsSegment(path, filter, m_engineRate, 2, renderStartMs, renderDurationMs));
    }
    rt->renderSampleRate = renderSampleRate;
    proc->setProcessChannelMode(QProcess::SeparateChannels);

    if (useRawInput) {
        connect(proc, &QProcess::started, this, [this, index, jobId]() {
            PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
            if (!rt || rt->renderJobId != jobId || !rt->renderProcess) {
                return;
            }
            if (!rt->renderInput.isEmpty()) {
                rt->renderProcess->write(rt->renderInput);
                rt->renderProcess->closeWriteChannel();
            }
        });
    }

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
                rt->renderInput.clear();
                auto buffer = decodePcm16(bytes, rt->renderSampleRate, 2);
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
    const bool synthPad = isSynth(index);
    if (path.isEmpty() && !synthPad) {
        return;
    }

    PadParams &params = m_params[static_cast<size_t>(index)];
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return;
    }

    if (synthPad) {
        if (!m_engineAvailable || !m_engine) {
            return;
        }
        const SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
        const int baseMidi = m_synthBaseMidi[static_cast<size_t>(index)];
        const int velocity = 127;
        const bool isDx7 =
            isMiniDexedType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]));
        m_engine->setSynthEnabled(index, true);
        m_engine->setSynthParams(index, params.volume, params.pan, params.fxBus);
        m_engine->synthAllNotesOff(index);
        m_engine->synthNoteOn(index, baseMidi, velocity);
        const int lengthMs = isDx7
                                 ? qBound(500, static_cast<int>(3000 + sp.release * 3500.0f), 8000)
                                 : qBound(80, static_cast<int>(300 + sp.release * 900.0f), 2000);
        QTimer::singleShot(lengthMs, this, [this, index, baseMidi]() {
            if (m_engine) {
                m_engine->synthNoteOff(index, baseMidi);
            }
        });
        return;
    }

    const double pitchRate = pitchToRate(params.pitch);
    const bool wantsProcessing = !synthPad && needsProcessing(params);
    const bool stretchEnabled = params.stretchIndex > 0;
    const bool stretchHq = stretchEnabled && params.stretchMode > 0;
    const float normalizeGain = (params.normalize && rt) ? rt->normalizeGain : 1.0f;
    if (rt->useEngine && m_engineAvailable && m_engine) {
        std::shared_ptr<AudioEngine::Buffer> buffer;
        bool useProcessed = false;
        if (wantsProcessing) {
            const RenderSignature sig = makeSignature(path, params, m_bpm);
            if (rt->processedReady && sig == rt->processedSignature) {
                buffer = rt->processedBuffer;
                useProcessed = true;
            } else {
                if (rt->rawBuffer && rt->rawBuffer->isValid()) {
                    buffer = rt->rawBuffer;
                    useProcessed = false;
                    scheduleProcessedRender(index);
                } else {
                    rt->pendingTrigger = true;
                    scheduleProcessedRender(index);
                    return;
                }
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

            if (useProcessed) {
                startFrame = 0;
                endFrame = totalFrames;
            }
            float tempoFactor = 1.0f;
            if (!useProcessed && stretchEnabled) {
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

            const float rate = useProcessed ? 1.0f : static_cast<float>(pitchRate) * tempoFactor;
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

    const bool wantsStretch = stretchHq && !isNear(tempoFactor, 1.0);
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

void PadBank::triggerPadMidi(int index, int midiNote, int lengthSteps) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (!isSynth(index)) {
        triggerPad(index);
        return;
    }
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return;
    }
    if (!m_engineAvailable || !m_engine) {
        return;
    }

    PadParams &params = m_params[static_cast<size_t>(index)];
    const int bpm = m_bpm;
    const int stepMs = 60000 / qMax(1, bpm) / 4;
    const int steps = qMax(1, lengthSteps);
    const int lengthMs = qBound(60, steps * stepMs, 4000);
    const int velocity = 127;
    m_engine->setSynthEnabled(index, true);
    m_engine->setSynthParams(index, params.volume, params.pan, params.fxBus);
    m_engine->synthNoteOn(index, midiNote, velocity);
    QTimer::singleShot(lengthMs, this, [this, index, midiNote]() {
        if (m_engine) {
            m_engine->synthNoteOff(index, midiNote);
        }
    });
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
        if (isSynth(index)) {
            m_engine->synthAllNotesOff(index);
        }
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
    if (m_engineAvailable && m_engine) {
        m_engine->setBpm(m_bpm);
    }
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

bool PadBank::startRecording(const QString &path, int durationMs, int targetRate) {
    if (!m_engineAvailable || !m_engine) {
        return false;
    }
    if (durationMs <= 0 || path.isEmpty()) {
        return false;
    }
    const int frames = qMax(1, static_cast<int>((durationMs * m_engineRate) / 1000.0));
    return m_engine->startRecording(path, frames, targetRate);
}

static std::shared_ptr<AudioEngine::Buffer> makeMetronomeBuffer(int sampleRate, float freq,
                                                                float lengthSec) {
    if (sampleRate <= 0) {
        sampleRate = 48000;
    }
    const int frames = qMax(1, static_cast<int>(sampleRate * lengthSec));
    auto buffer = std::make_shared<AudioEngine::Buffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->samples.resize(frames * buffer->channels);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / sampleRate;
        const float env = std::exp(-t * 12.0f);
        const float v = std::sin(2.0f * static_cast<float>(M_PI) * freq * t) * env;
        buffer->samples[i * 2] = v;
        buffer->samples[i * 2 + 1] = v;
    }
    return buffer;
}

void PadBank::triggerMetronome(bool accent) {
    if (!m_engineAvailable || !m_engine) {
        return;
    }
    if (!m_metronomeBuffer || !m_metronomeAccent) {
        m_metronomeBuffer = makeMetronomeBuffer(m_engineRate, 1600.0f, 0.05f);
        m_metronomeAccent = makeMetronomeBuffer(m_engineRate, 2200.0f, 0.06f);
    }
    auto buffer = accent ? m_metronomeAccent : m_metronomeBuffer;
    if (!buffer || !buffer->isValid()) {
        return;
    }
    m_engine->trigger(-1, buffer, 0, buffer->frames(), false, 0.6f, 0.0f, 1.0f, 0);
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
    scanDx7Banks();
    if (g_dx7Banks.isEmpty()) {
        return {"PROGRAM 01"};
    }
    const QStringList programs = g_dx7Banks.front().programs;
    if (!programs.isEmpty()) {
        return programs;
    }
    return {"PROGRAM 01"};
}

QStringList PadBank::synthBanks() {
    scanDx7Banks();
    QStringList list;
    for (const auto &bank : g_dx7Banks) {
        list << bank.name;
    }
    if (list.isEmpty()) {
        list << "INTERNAL";
    }
    list << "SERUM";
    return list;
}

QStringList PadBank::synthPresetsForBank(const QString &bank) {
    const QString upper = bank.trimmed().toUpper();
    if (upper == "FM" || upper == "SERUM") {
        const QStringList presets = fmPresetNames();
        return presets.isEmpty() ? QStringList{"INIT"} : presets;
    }
    scanDx7Banks();
    if (g_dx7Banks.isEmpty()) {
        return {"PROGRAM 01"};
    }
    int index = bankIndexForName(bank);
    if (index < 0 || index >= g_dx7Banks.size()) {
        index = 0;
    }
    const QStringList programs = g_dx7Banks[index].programs;
    if (!programs.isEmpty()) {
        return programs;
    }
    return {"PROGRAM 01"};
}

QStringList PadBank::serumWaves() {
    return {"SINE", "SAW", "SQUARE", "TRI", "NOISE",
            "PWM", "SUPERSAW", "BELL", "FORMANT", "METAL"};
}

QStringList PadBank::synthTypes() {
    return {defaultMiniDexedType(), "SERUM"};
}

bool PadBank::hasMiniDexed() {
    return false;
}

int PadBank::synthVoiceParam(int index, int param) const {
    if (index < 0 || index >= padCount()) {
        return 0;
    }
    if (!isSynth(index) || !m_engineAvailable || !m_engine) {
        return 0;
    }
    return m_engine->synthVoiceParam(index, param);
}

void PadBank::setSynthVoiceParam(int index, int param, int value) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (!isSynth(index) || !m_engineAvailable || !m_engine) {
        return;
    }
    if (m_engine->setSynthVoiceParam(index, param, value)) {
        emit padParamsChanged(index);
    }
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
    if (isSynth(index) && m_engineAvailable && m_engine) {
        const PadParams &pp = m_params[static_cast<size_t>(index)];
        m_engine->setSynthParams(index, pp.volume, pp.pan, pp.fxBus);
    }
    emit padParamsChanged(index);
}

QString PadBank::fxBusLabel(int index) {
    const int maxIndex = static_cast<int>(sizeof(kFxBusLabels) / sizeof(kFxBusLabels[0])) - 1;
    const int idx = qBound(0, index, maxIndex);
    return QString::fromLatin1(kFxBusLabels[idx]);
}
