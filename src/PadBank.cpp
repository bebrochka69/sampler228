#include "PadBank.h"

#include "AudioEngine.h"

#include <QAudioOutput>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaPlayer>
#include <QProcess>
#include <QRegularExpression>
#include <QFile>
#include <QSoundEffect>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtGlobal>
#include <QtMath>
#include <QDirIterator>
#include <cmath>
#include <cstdint>
#include <mutex>

#ifdef GROOVEBOX_WITH_JACK
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
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

QString defaultHexterType();

QString synthTypeFromName(const QString &name) {
    const QString upper = name.trimmed().toUpper();
    if (upper.startsWith("HEXTER")) {
        return "HEXTER";
    }
    if (upper.contains(":")) {
        const int colon = upper.indexOf(':');
        return upper.left(colon).trimmed();
    }
    return defaultHexterType();
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

bool isHexterType(const QString &type) {
    const QString t = type.trimmed().toUpper();
    return t == "HEXTER";
}

QString defaultHexterType() {
#ifdef Q_OS_LINUX
    if (!QStandardPaths::findExecutable("hexter").isEmpty()) {
        return QStringLiteral("HEXTER");
    }
    if (!QStandardPaths::findExecutable("jack-dssi-host").isEmpty()) {
        return QStringLiteral("HEXTER");
    }
#endif
    return QStringLiteral("HEXTER");
}

struct HexterPresetInfo {
    QString name;
    int program = 0;
    int note = 60;
};

float clamp01(float value) {
    return qBound(0.0f, value, 1.0f);
}

struct HexterPreset {
    QString display;
    int program = 0;
};

static QVector<HexterPreset> g_hexterPresets;
static bool g_hexterScanned = false;

static void scanHexterPresets() {
    if (g_hexterScanned) {
        return;
    }
    g_hexterScanned = true;
    g_hexterPresets.clear();
    for (int i = 0; i < 32; ++i) {
        HexterPreset preset;
        preset.program = i;
        preset.display = QString("PROGRAM %1").arg(i + 1, 2, 10, QChar('0'));
        g_hexterPresets.push_back(preset);
    }
}

static int hexterProgramForPreset(const QString &display) {
    const QString trimmed = display.trimmed();
    QRegularExpression re("(\\d+)");
    const QRegularExpressionMatch m = re.match(trimmed);
    if (m.hasMatch()) {
        bool ok = false;
        int value = m.captured(1).toInt(&ok);
        if (ok) {
            return qBound(0, value - 1, 127);
        }
    }
    scanHexterPresets();
    for (const auto &preset : g_hexterPresets) {
        if (preset.display.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return preset.program;
        }
    }
    return 0;
}

#ifdef GROOVEBOX_WITH_JACK
struct JackMidiEvent {
    uint32_t size = 0;
    uint32_t time = 0;
    uint8_t data[3] = {0, 0, 0};
};

struct JackMidiEngine {
    jack_client_t *client = nullptr;
    jack_port_t *port = nullptr;
    jack_ringbuffer_t *ring = nullptr;
    bool active = false;
    bool connected = false;
    QString targetPort;
};

static JackMidiEngine g_jackMidi;

static int jackProcessCallback(jack_nframes_t nframes, void *arg) {
    auto *eng = static_cast<JackMidiEngine *>(arg);
    if (!eng || !eng->port) {
        return 0;
    }
    void *buf = jack_port_get_buffer(eng->port, nframes);
    jack_midi_clear_buffer(buf);
    if (!eng->ring) {
        return 0;
    }
    while (jack_ringbuffer_read_space(eng->ring) >= sizeof(JackMidiEvent)) {
        JackMidiEvent ev;
        jack_ringbuffer_read(eng->ring, reinterpret_cast<char *>(&ev), sizeof(JackMidiEvent));
        const jack_nframes_t offset = ev.time < nframes ? ev.time : 0;
        jack_midi_event_write(buf, offset, ev.data, ev.size);
    }
    return 0;
}

static bool ensureJackMidiReady() {
    if (g_jackMidi.active && g_jackMidi.client && g_jackMidi.port) {
        return true;
    }
    jack_status_t status = static_cast<jack_status_t>(0);
    g_jackMidi.client = jack_client_open("GrooveBox", JackNullOption, &status);
    if (!g_jackMidi.client) {
        g_jackMidi.client = jack_client_open("GrooveBoxMidi", JackNullOption, &status);
    }
    if (!g_jackMidi.client) {
        return false;
    }
    g_jackMidi.port = jack_port_register(g_jackMidi.client, "midi_out",
                                         JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
    if (!g_jackMidi.port) {
        jack_client_close(g_jackMidi.client);
        g_jackMidi.client = nullptr;
        return false;
    }
    g_jackMidi.ring = jack_ringbuffer_create(16384);
    if (!g_jackMidi.ring) {
        return false;
    }
    jack_set_process_callback(g_jackMidi.client, jackProcessCallback, &g_jackMidi);
    if (jack_activate(g_jackMidi.client) != 0) {
        return false;
    }
    g_jackMidi.active = true;
    g_jackMidi.connected = false;
    return true;
}

static bool jackSendMidi(const uint8_t *data, uint32_t size, uint32_t time = 0) {
    if (!ensureJackMidiReady() || !g_jackMidi.ring) {
        return false;
    }
    if (jack_ringbuffer_write_space(g_jackMidi.ring) < sizeof(JackMidiEvent)) {
        return false;
    }
    JackMidiEvent ev;
    ev.size = size;
    ev.time = time;
    ev.data[0] = data[0];
    ev.data[1] = size > 1 ? data[1] : 0;
    ev.data[2] = size > 2 ? data[2] : 0;
    jack_ringbuffer_write(g_jackMidi.ring, reinterpret_cast<const char *>(&ev), sizeof(JackMidiEvent));
    return true;
}

static void jackSendNote(int note, int vel, bool on) {
    const uint8_t status = static_cast<uint8_t>(on ? 0x90 : 0x80);
    uint8_t data[3] = {status, static_cast<uint8_t>(note & 0x7F),
                       static_cast<uint8_t>(vel & 0x7F)};
    jackSendMidi(data, 3, 0);
}

static void jackSendProgram(int program) {
    const uint8_t status = 0xC0;
    uint8_t data[2] = {status, static_cast<uint8_t>(program & 0x7F)};
    jackSendMidi(data, 2, 0);
}

struct HexterEngine {
    QProcess *proc = nullptr;
    QString presetName;
    int program = 0;
    bool ready = false;
    int pendingNote = -1;
    int pendingVelocity = 0;
    int pendingLengthMs = 0;
};

static HexterEngine g_hexterEngine;

static QString findHexterCommand(QStringList &args) {
#ifdef Q_OS_LINUX
    const QString custom = qEnvironmentVariable("GROOVEBOX_HEXTER_CMD");
    if (!custom.isEmpty()) {
        return custom;
    }
    const QString host = QStandardPaths::findExecutable("jack-dssi-host");
    QString plugin = qEnvironmentVariable("GROOVEBOX_HEXTER_PLUGIN");
    if (plugin.isEmpty()) {
        const QStringList candidates = {
            "/usr/lib/dssi/hexter.so",
            "/usr/local/lib/dssi/hexter.so",
            "/usr/lib64/dssi/hexter.so"
        };
        for (const QString &cand : candidates) {
            if (QFileInfo::exists(cand)) {
                plugin = cand;
                break;
            }
        }
    }
    if (!host.isEmpty() && !plugin.isEmpty()) {
        args << plugin;
        return host;
    }
    const QString standalone = QStandardPaths::findExecutable("hexter");
    if (!standalone.isEmpty()) {
        return standalone;
    }
#endif
    return QString();
}

static bool jackConnectHexter(bool connectAudio) {
    if (!g_jackMidi.client || !g_jackMidi.port) {
        return false;
    }
    if (g_jackMidi.connected && !g_jackMidi.targetPort.isEmpty()) {
        return true;
    }
    const QString envTarget = qEnvironmentVariable("GROOVEBOX_HEXTER_MIDI_PORT");
    const char *src = jack_port_name(g_jackMidi.port);
    if (!src) {
        return false;
    }
    const char **ports = nullptr;
    if (!envTarget.isEmpty()) {
        const QByteArray pattern = envTarget.toLocal8Bit();
        ports = jack_get_ports(g_jackMidi.client, pattern.constData(),
                               JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
    }
    if (!ports || !ports[0]) {
        if (ports) {
            jack_free(ports);
        }
        ports = jack_get_ports(g_jackMidi.client, "hexter.*midi.*in",
                               JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
    }
    if (!ports || !ports[0]) {
        if (ports) {
            jack_free(ports);
        }
        ports = jack_get_ports(g_jackMidi.client, "Hexter.*midi.*in",
                               JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
    }
    if (!ports || !ports[0]) {
        if (ports) {
            jack_free(ports);
        }
        ports = jack_get_ports(g_jackMidi.client, "jack-dssi-host.*midi.*in",
                               JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
    }
    if (ports && ports[0]) {
        jack_connect(g_jackMidi.client, src, ports[0]);
        g_jackMidi.connected = true;
        g_jackMidi.targetPort = QString::fromLocal8Bit(ports[0]);
    }
    if (ports) {
        jack_free(ports);
    }
    if (!g_jackMidi.connected) {
        qWarning() << "Hexter MIDI port not found";
    }
    if (connectAudio && g_jackMidi.client) {
        const char **hL = jack_get_ports(g_jackMidi.client, "hexter.*left",
                                         JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        const char **hR = jack_get_ports(g_jackMidi.client, "hexter.*right",
                                         JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        const char **p1 = jack_get_ports(g_jackMidi.client, "system:playback_1",
                                         JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
        const char **p2 = jack_get_ports(g_jackMidi.client, "system:playback_2",
                                         JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
        if (hL && hL[0] && p1 && p1[0]) {
            jack_connect(g_jackMidi.client, hL[0], p1[0]);
        }
        if (hR && hR[0] && p2 && p2[0]) {
            jack_connect(g_jackMidi.client, hR[0], p2[0]);
        }
        if (hL) jack_free(hL);
        if (hR) jack_free(hR);
        if (p1) jack_free(p1);
        if (p2) jack_free(p2);
    }
    return g_jackMidi.connected;
}

static bool ensureHexterRunning(const QString &presetName, int program, int sampleRate) {
    Q_UNUSED(sampleRate);
    if (!g_hexterEngine.proc) {
        g_hexterEngine.proc = new QProcess();
    }
    if (g_hexterEngine.proc->state() == QProcess::Running) {
        g_hexterEngine.presetName = presetName;
        g_hexterEngine.program = program;
        if (g_hexterEngine.ready) {
            jackSendProgram(program);
        }
        return g_hexterEngine.ready;
    }

    QStringList args;
    const QString cmd = findHexterCommand(args);
    if (cmd.isEmpty()) {
        qWarning() << "Hexter command not found";
        return false;
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    g_hexterEngine.proc->setProcessEnvironment(env);
    g_hexterEngine.proc->setProgram(cmd);
    g_hexterEngine.proc->setArguments(args);
    g_hexterEngine.proc->setProcessChannelMode(QProcess::MergedChannels);
    g_hexterEngine.ready = false;
    g_hexterEngine.presetName = presetName;
    g_hexterEngine.program = program;
    g_hexterEngine.proc->start();
    if (!ensureJackMidiReady()) {
        return false;
    }
    g_hexterEngine.ready = jackConnectHexter(false);
    if (g_hexterEngine.ready) {
        jackSendProgram(program);
    }
    return g_hexterEngine.ready;
}

static void queueHexterNote(int note, int velocity, int lengthMs) {
    g_hexterEngine.pendingNote = note;
    g_hexterEngine.pendingVelocity = velocity;
    g_hexterEngine.pendingLengthMs = lengthMs;
}
#endif

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
        m_synthBaseMidi[static_cast<size_t>(i)] = 60;
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

#ifdef GROOVEBOX_WITH_JACK
    ensureJackMidiReady();
    scanHexterPresets();
    if (hasHexter()) {
        const QString preset = g_hexterPresets.isEmpty() ? QString("PROGRAM 01")
                                                         : g_hexterPresets.first().display;
        const int program = hexterProgramForPreset(preset);
        ensureHexterRunning(preset, program, m_engineRate);
    }
    m_synthConnectTimer = new QTimer(this);
    m_synthConnectTimer->setInterval(300);
    connect(m_synthConnectTimer, &QTimer::timeout, this, [this]() {
        if (!g_jackMidi.active) {
            ensureJackMidiReady();
        }
        int padIndex = -1;
        for (int i = 0; i < kPadCount; ++i) {
            if (m_isSynth[static_cast<size_t>(i)] &&
                isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(i)]))) {
                padIndex = i;
                break;
            }
        }
        if (padIndex < 0) {
            return;
        }
        const QString preset = synthPresetFromName(m_synthNames[static_cast<size_t>(padIndex)]);
        const int program = hexterProgramForPreset(preset);
        ensureHexterRunning(preset, program, m_engineRate);
        if (g_hexterEngine.pendingNote >= 0) {
            const int note = g_hexterEngine.pendingNote;
            const int vel = g_hexterEngine.pendingVelocity;
            const int lengthMs = g_hexterEngine.pendingLengthMs;
            g_hexterEngine.pendingNote = -1;
            jackSendNote(note, vel, true);
            QTimer::singleShot(lengthMs, [note]() { jackSendNote(note, 0, false); });
        }
    });
    m_synthConnectTimer->start();
#endif
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
#ifdef GROOVEBOX_WITH_JACK
    if (g_hexterEngine.proc) {
        g_hexterEngine.proc->kill();
        g_hexterEngine.proc->deleteLater();
        g_hexterEngine.proc = nullptr;
    }
#endif
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
    if (m_engineAvailable && m_engine) {
        m_engine->setPadAdsr(index, 0.0f, 0.0f, 1.0f, 0.0f);
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
    const QString preset = synthPresetFromName(raw);
    return preset.isEmpty() ? defaultHexterType() : preset;
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
    QString synthName = name;
    const QString type = synthTypeFromName(name);
    if (!name.contains(":") || !isHexterType(type)) {
        synthName = makeSynthName(defaultHexterType(), QString("PROGRAM 01"));
    }

    m_isSynth[static_cast<size_t>(index)] = true;
    m_synthNames[static_cast<size_t>(index)] = synthName;
    m_paths[static_cast<size_t>(index)].clear();
    m_synthBaseMidi[static_cast<size_t>(index)] = 60;

    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt) {
        if (isHexterType(type)) {
            rt->rawBuffer.reset();
            rt->processedBuffer.reset();
            rt->processedReady = false;
            rt->pendingProcessed = false;
            rt->rawPath = QString("synth:%1").arg(synthName);
            rt->rawDurationMs = 0;
            rt->durationMs = 0;
            rt->normalizeGain = 1.0f;
#ifdef GROOVEBOX_WITH_JACK
            const QString preset = synthPresetFromName(synthName);
            const int program = hexterProgramForPreset(preset);
            ensureHexterRunning(preset, program, m_engineRate);
#endif
        }
    }
    if (m_engineAvailable && m_engine) {
        const SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
        m_engine->setPadAdsr(index, sp.attack, sp.decay, sp.sustain, sp.release);
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
    if (isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
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
    if (isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
        return;
    }
    SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
    sp.voices = qBound(1, voices, 8);
    if (isSynth(index)) {
        PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
        rebuildSynthRuntime(rt, m_synthNames[static_cast<size_t>(index)], m_engineRate,
                            m_synthBaseMidi[static_cast<size_t>(index)], sp);
    }
    emit padParamsChanged(index);
}

void PadBank::setSynthDetune(int index, float detune) {
    if (index < 0 || index >= padCount()) {
        return;
    }
    if (isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
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
    if (isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
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
    if (isSynth(index)) {
        if (isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
            return true;
        }
        return rt->rawBuffer && rt->rawBuffer->isValid();
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
    const bool synthPad = isSynth(index);
    if (path.isEmpty() && !synthPad) {
        return;
    }

    PadParams &params = m_params[static_cast<size_t>(index)];
    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (!rt) {
        return;
    }

    if (synthPad && isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
#ifdef GROOVEBOX_WITH_JACK
        const SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
        const QString presetName = synthPresetFromName(m_synthNames[static_cast<size_t>(index)]);
        const int program = hexterProgramForPreset(presetName);
        if (ensureHexterRunning(presetName, program, m_engineRate)) {
            const int baseMidi = m_synthBaseMidi[static_cast<size_t>(index)];
            const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
            jackSendProgram(program);
            jackSendNote(baseMidi, velocity, true);
            const int lengthMs =
                qBound(80, static_cast<int>(300 + sp.release * 900.0f), 2000);
            QTimer::singleShot(lengthMs, this, [baseMidi]() { jackSendNote(baseMidi, 0, false); });
            return;
        }
        const int baseMidi = m_synthBaseMidi[static_cast<size_t>(index)];
        const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
        const int lengthMs =
            qBound(80, static_cast<int>(300 + sp.release * 900.0f), 2000);
        queueHexterNote(baseMidi, velocity, lengthMs);
        return;
#endif
    }

    const double pitchRate = pitchToRate(params.pitch);
    const bool wantsProcessing = !synthPad && needsProcessing(params);
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
            if (synthPad) {
                const int baseMidi = m_synthBaseMidi[static_cast<size_t>(index)];
                buffer = buildSynthBuffer(m_synthNames[static_cast<size_t>(index)], m_engineRate, baseMidi,
                                          m_synthParams[static_cast<size_t>(index)]);
                rt->rawBuffer = buffer;
                rt->processedBuffer = buffer;
                rt->processedReady = true;
            } else {
                rt->pendingTrigger = true;
                scheduleRawRender(index);
                return;
            }
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
    if (isHexterType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
#ifdef GROOVEBOX_WITH_JACK
        const QString presetName = synthPresetFromName(m_synthNames[static_cast<size_t>(index)]);
        const int program = hexterProgramForPreset(presetName);
        if (ensureHexterRunning(presetName, program, m_engineRate)) {
            PadParams &params = m_params[static_cast<size_t>(index)];
            const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
            const int bpm = m_bpm;
            const int stepMs = 60000 / qMax(1, bpm) / 4;
            const int steps = qMax(1, lengthSteps);
            const int lengthMs = qBound(60, steps * stepMs, 4000);
            jackSendProgram(program);
            jackSendNote(midiNote, velocity, true);
            QTimer::singleShot(lengthMs, this, [midiNote]() { jackSendNote(midiNote, 0, false); });
            return;
        }
        PadParams &params = m_params[static_cast<size_t>(index)];
        const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
        const int bpm = m_bpm;
        const int stepMs = 60000 / qMax(1, bpm) / 4;
        const int steps = qMax(1, lengthSteps);
        const int lengthMs = qBound(60, steps * stepMs, 4000);
        queueHexterNote(midiNote, velocity, lengthMs);
        return;
#endif
    }
    if (!rt->useEngine || !m_engineAvailable || !m_engine) {
        return;
    }

    const int baseMidi = m_synthBaseMidi[static_cast<size_t>(index)];
    const QString name = m_synthNames[static_cast<size_t>(index)];
    if (!rt->rawBuffer || !rt->rawBuffer->isValid()) {
        rt->rawBuffer = buildSynthBuffer(name, m_engineRate, baseMidi,
                                         m_synthParams[static_cast<size_t>(index)]);
        rt->processedBuffer = rt->rawBuffer;
        rt->processedReady = true;
    }
    auto buffer = rt->rawBuffer;
    if (!buffer || !buffer->isValid()) {
        return;
    }

    PadParams &params = m_params[static_cast<size_t>(index)];
    const float semis = params.pitch + static_cast<float>(midiNote - baseMidi);
    const float rate = static_cast<float>(pitchToRate(semis));

    const int bpm = m_bpm;
    const int stepMs = 60000 / qMax(1, bpm) / 4;
    const int steps = qMax(1, lengthSteps);
    const qint64 lengthMs = qMax<qint64>(1, static_cast<qint64>(steps) * stepMs);
    int lengthFrames = static_cast<int>(
        static_cast<double>(lengthMs) * buffer->sampleRate * rate / 1000.0);
    lengthFrames = qMax(1, lengthFrames);

    const int totalFrames = buffer->frames();
    bool loop = false;
    int endFrame = qMin(totalFrames, lengthFrames);
    if (lengthFrames > totalFrames) {
        endFrame = totalFrames;
        loop = true;
    }

    m_engine->trigger(index, buffer, 0, endFrame, loop, params.volume,
                      params.pan, rate, params.fxBus);

    if (loop) {
        const int token = ++rt->synthStopToken;
        QTimer::singleShot(static_cast<int>(lengthMs), this, [this, index, token]() {
            PadRuntime *rtLocal = m_runtime[static_cast<size_t>(index)];
            if (!rtLocal || rtLocal->synthStopToken != token) {
                return;
            }
            stopPad(index);
        });
    }
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
    scanHexterPresets();
    QStringList list;
    for (const auto &preset : g_hexterPresets) {
        list << preset.display;
    }
    if (!list.isEmpty()) {
        return list;
    }
    list << "PROGRAM 01";
    return list;
}

QStringList PadBank::serumWaves() {
    return {"SINE", "SAW", "SQUARE", "TRI", "NOISE"};
}

QStringList PadBank::synthTypes() {
    return {defaultHexterType()};
}

bool PadBank::hasHexter() {
#ifdef Q_OS_LINUX
    if (!QStandardPaths::findExecutable("hexter").isEmpty()) {
        return true;
    }
    if (!QStandardPaths::findExecutable("jack-dssi-host").isEmpty()) {
        return true;
    }
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
