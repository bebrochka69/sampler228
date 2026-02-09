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
#include <QHostAddress>
#include <QUdpSocket>
#include <cmath>
#include <cstdint>
#include <mutex>

#ifdef GROOVEBOX_WITH_FLUIDSYNTH
#include <fluidsynth.h>
#endif

#ifdef GROOVEBOX_WITH_ALSA
#include <alsa/asoundlib.h>
#endif
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

QString defaultZynLikeType();

QString synthTypeFromName(const QString &name) {
    const QString upper = name.trimmed().toUpper();
    if (upper.startsWith("YOSHIMI")) {
        return "YOSHIMI";
    }
    if (upper.startsWith("ZYN") || upper.startsWith("ZYNADDSUBFX")) {
        return "ZYN";
    }
    if (upper.contains(":")) {
        const int colon = upper.indexOf(':');
        return upper.left(colon).trimmed();
    }
    return defaultZynLikeType();
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

bool isZynLikeType(const QString &type) {
    const QString t = type.trimmed().toUpper();
    return t == "ZYN" || t == "ZYNADDSUBFX" || t == "YOSHIMI";
}

QString defaultZynLikeType() {
#ifdef Q_OS_LINUX
    if (!QStandardPaths::findExecutable("yoshimi").isEmpty()) {
        return QStringLiteral("YOSHIMI");
    }
    if (!QStandardPaths::findExecutable("zynaddsubfx").isEmpty()) {
        return QStringLiteral("ZYN");
    }
#endif
    return QStringLiteral("ZYN");
}

struct SynthPresetInfo {
    const char *name;
    int bank;
    int program;
    int note;
};

constexpr SynthPresetInfo kSynthPresets[] = {
    {"KEYS/PIANO 1", 0, 0, 60},
    {"KEYS/PIANO 2", 0, 1, 60},
    {"KEYS/E.PIANO", 0, 4, 60},
    {"ORGANS/DRAWBAR", 0, 16, 60},
    {"BASS/FINGER", 0, 32, 48},
    {"LEADS/SAW", 0, 80, 72},
    {"PADS/WARM", 0, 89, 60},
    {"STRINGS/ENSEMBLE", 0, 48, 60},
    {"BRASS/SECTION", 0, 56, 60},
};

float clamp01(float value) {
    return qBound(0.0f, value, 1.0f);
}

struct ZynPreset {
    QString display;
    QString category;
    QString path;
};

static QVector<ZynPreset> g_zynPresets;
static bool g_zynScanned = false;

static void scanZynPresets() {
    if (g_zynScanned) {
        return;
    }
    g_zynScanned = true;
    QStringList roots;
#ifdef Q_OS_LINUX
    roots << "/usr/share/yoshimi/instruments"
          << "/usr/local/share/yoshimi/instruments"
          << "/usr/share/yoshimi/banks"
          << "/usr/local/share/yoshimi/banks"
          << "/usr/share/zynaddsubfx/instruments"
          << "/usr/local/share/zynaddsubfx/instruments"
          << "/usr/share/zynaddsubfx/banks"
          << "/usr/local/share/zynaddsubfx/banks";
#endif
    roots << QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../zynaddsubfx-master/instruments");

    QStringList found;
    for (const QString &root : roots) {
        if (!QFileInfo::exists(root)) {
            continue;
        }
        QDirIterator it(root, QStringList() << "*.xiz", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            found << it.next();
        }
    }

    for (const QString &path : found) {
        QString rel = path;
        for (const QString &root : roots) {
            if (path.startsWith(root)) {
                rel = path.mid(root.length());
                if (rel.startsWith('/') || rel.startsWith('\\')) {
                    rel.remove(0, 1);
                }
                break;
            }
        }
        QStringList parts = rel.split(QRegularExpression("[/\\\\]"), Qt::SkipEmptyParts);
        QString category = parts.isEmpty() ? QString("MISC") : parts.first().toUpper();
        QString name = parts.isEmpty() ? QFileInfo(path).baseName()
                                       : QFileInfo(parts.last()).completeBaseName();
        ZynPreset preset;
        preset.category = category;
        preset.display = category + "/" + name.toUpper();
        preset.path = path;
        g_zynPresets.push_back(preset);
    }

    if (g_zynPresets.isEmpty()) {
        // Fallback list if no preset files are found
        QStringList fallback = {"KEYS/PIANO 1", "KEYS/PIANO 2", "LEADS/SAW",
                                "BASS/FINGER", "PADS/WARM"};
        for (const QString &name : fallback) {
            ZynPreset preset;
            preset.category = name.split('/').value(0, "MISC").toUpper();
            preset.display = name.toUpper();
            preset.path = QString();
            g_zynPresets.push_back(preset);
        }
    }
}

static QString zynPathForPreset(const QString &display) {
    scanZynPresets();
    const QString key = display.trimmed().toUpper();
    for (const auto &preset : g_zynPresets) {
        if (preset.display.toUpper() == key) {
            return preset.path;
        }
    }
    return QString();
}

#ifdef GROOVEBOX_WITH_ALSA
struct ZynEngine {
    QProcess *proc = nullptr;
    QString presetPath;
    QString presetName;
    QString engineLabel;
    bool isYoshimi = false;
    int oscPort = 0;
    QString lastLoadedPath;
    bool ready = false;
    QByteArray stdoutBuffer;
    snd_seq_t *seq = nullptr;
    int outPort = -1;
    int destClient = -1;
    int destPort = -1;
    int pendingNote = -1;
    int pendingVelocity = 0;
    int pendingLengthMs = 0;
};

static ZynEngine g_zynEngine;

static void tryAconnectZyn();

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
    jack_status_t status = 0;
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

static bool jackConnectYoshimi(bool connectAudio) {
    if (!g_jackMidi.client || !g_jackMidi.port) {
        return false;
    }
    if (g_jackMidi.connected) {
        return true;
    }
    const char **ports = jack_get_ports(
        g_jackMidi.client, "yoshimi.*midi.*in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
    if (!ports || !ports[0]) {
        if (ports) {
            jack_free(ports);
        }
        ports = jack_get_ports(
            g_jackMidi.client, "Yoshimi.*midi.*in", JACK_DEFAULT_MIDI_TYPE, JackPortIsInput);
    }
    if (ports && ports[0]) {
        const char *src = jack_port_name(g_jackMidi.port);
        if (src) {
            jack_connect(g_jackMidi.client, src, ports[0]);
            g_jackMidi.connected = true;
            g_jackMidi.targetPort = QString::fromLocal8Bit(ports[0]);
        }
    }
    if (ports) {
        jack_free(ports);
    }
    if (connectAudio && g_jackMidi.client) {
        const char **yL = jack_get_ports(g_jackMidi.client, "yoshimi.*left", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        const char **yR = jack_get_ports(g_jackMidi.client, "yoshimi.*right", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
        const char **p1 = jack_get_ports(g_jackMidi.client, "system:playback_1", JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsInput);
        const char **p2 = jack_get_ports(g_jackMidi.client, "system:playback_2", JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsInput);
        if (yL && yL[0] && p1 && p1[0]) {
            jack_connect(g_jackMidi.client, yL[0], p1[0]);
        }
        if (yR && yR[0] && p2 && p2[0]) {
            jack_connect(g_jackMidi.client, yR[0], p2[0]);
        }
        if (yL) jack_free(yL);
        if (yR) jack_free(yR);
        if (p1) jack_free(p1);
        if (p2) jack_free(p2);
    }
    return g_jackMidi.connected;
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
#endif

static void queueZynNote(int note, int velocity, int lengthMs) {
    g_zynEngine.pendingNote = note;
    g_zynEngine.pendingVelocity = velocity;
    g_zynEngine.pendingLengthMs = lengthMs;
}

static void appendOscString(QByteArray &buf, const QByteArray &value) {
    buf.append(value);
    buf.append('\0');
    while (buf.size() % 4) {
        buf.append('\0');
    }
}

static void appendOscInt(QByteArray &buf, int value) {
    unsigned char bytes[4];
    bytes[0] = static_cast<unsigned char>((value >> 24) & 0xFF);
    bytes[1] = static_cast<unsigned char>((value >> 16) & 0xFF);
    bytes[2] = static_cast<unsigned char>((value >> 8) & 0xFF);
    bytes[3] = static_cast<unsigned char>(value & 0xFF);
    buf.append(reinterpret_cast<const char *>(bytes), 4);
}

static void sendZynOscLoad(const QString &path) {
    if (path.isEmpty()) {
        return;
    }
    int envPort = qEnvironmentVariableIntValue("GROOVEBOX_YOSHIMI_OSC_PORT");
    if (envPort <= 0) {
        envPort = qEnvironmentVariableIntValue("GROOVEBOX_ZYN_OSC_PORT");
    }
    const int port = envPort > 0 ? envPort : (g_zynEngine.oscPort > 0 ? g_zynEngine.oscPort : 12221);
    const QByteArray addr = "/load_xiz";
    const QByteArray str = path.toUtf8();

    QByteArray msgSi;
    appendOscString(msgSi, addr);
    appendOscString(msgSi, QByteArray(",si"));
    appendOscString(msgSi, str);
    appendOscInt(msgSi, 0);

    QByteArray msgIs;
    appendOscString(msgIs, addr);
    appendOscString(msgIs, QByteArray(",is"));
    appendOscInt(msgIs, 0);
    appendOscString(msgIs, str);

    QUdpSocket sock;
    sock.writeDatagram(msgSi, QHostAddress::LocalHost, port);
    sock.writeDatagram(msgIs, QHostAddress::LocalHost, port);
}

static void sendZynOscEnablePart(int part, int enabled) {
    int envPort = qEnvironmentVariableIntValue("GROOVEBOX_YOSHIMI_OSC_PORT");
    if (envPort <= 0) {
        envPort = qEnvironmentVariableIntValue("GROOVEBOX_ZYN_OSC_PORT");
    }
    const int port = envPort > 0 ? envPort : (g_zynEngine.oscPort > 0 ? g_zynEngine.oscPort : 12221);
    const QByteArray addr = QByteArray("/part") + QByteArray::number(part) + "/Penabled";
    QByteArray msg;
    appendOscString(msg, addr);
    appendOscString(msg, QByteArray(",i"));
    appendOscInt(msg, enabled ? 1 : 0);
    QUdpSocket sock;
    sock.writeDatagram(msg, QHostAddress::LocalHost, port);
}

static void sendZynOscPartVolume(int part, int volume) {
    int envPort = qEnvironmentVariableIntValue("GROOVEBOX_YOSHIMI_OSC_PORT");
    if (envPort <= 0) {
        envPort = qEnvironmentVariableIntValue("GROOVEBOX_ZYN_OSC_PORT");
    }
    const int port = envPort > 0 ? envPort : (g_zynEngine.oscPort > 0 ? g_zynEngine.oscPort : 12221);
    const QByteArray addr = QByteArray("/part") + QByteArray::number(part) + "/Pvolume";
    QByteArray msg;
    appendOscString(msg, addr);
    appendOscString(msg, QByteArray(",i"));
    appendOscInt(msg, qBound(0, volume, 127));
    QUdpSocket sock;
    sock.writeDatagram(msg, QHostAddress::LocalHost, port);
}

static void parseZynOutput(const QByteArray &chunk) {
    if (chunk.isEmpty()) {
        return;
    }
    g_zynEngine.stdoutBuffer.append(chunk);
    int idx = 0;
    while ((idx = g_zynEngine.stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray lineBytes = g_zynEngine.stdoutBuffer.left(idx);
        g_zynEngine.stdoutBuffer.remove(0, idx + 1);
        const QString line = QString::fromUtf8(lineBytes).trimmed();
        if (line.contains("lo server running on", Qt::CaseInsensitive)) {
            const QRegularExpression re("lo server running on\\s+(\\d+)");
            const QRegularExpressionMatch m = re.match(line);
            if (m.hasMatch()) {
                bool ok = false;
                const int port = m.captured(1).toInt(&ok);
                if (ok) {
                    g_zynEngine.oscPort = port;
                }
            }
        }
    }
}

static bool findZynPort(snd_seq_t *seq, int &clientOut, int &portOut) {
    snd_seq_client_info_t *cinfo;
    snd_seq_port_info_t *pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        const int client = snd_seq_client_info_get_client(cinfo);
        const char *name = snd_seq_client_info_get_name(cinfo);
        if (!name) {
            continue;
        }
        QString qname = QString::fromLocal8Bit(name).toUpper();
        if (!qname.contains("ZYN") && !qname.contains("YOSHIMI")) {
            continue;
        }
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            const unsigned int caps = snd_seq_port_info_get_capability(pinfo);
            if (!(caps & SND_SEQ_PORT_CAP_WRITE) || !(caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) {
                continue;
            }
            clientOut = client;
            portOut = snd_seq_port_info_get_port(pinfo);
            return true;
        }
    }
    return false;
}

static void ensureAlsaSequencerLoaded() {
#ifdef Q_OS_LINUX
    QProcess proc;
    proc.start("modprobe", QStringList() << "snd_seq");
    proc.waitForFinished(2000);
    proc.start("modprobe", QStringList() << "snd_seq_midi");
    proc.waitForFinished(2000);
#endif
}

static bool ensureZynMidiReady() {
    if (g_zynEngine.seq) {
        return true;
    }
    if (snd_seq_open(&g_zynEngine.seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        ensureAlsaSequencerLoaded();
        if (snd_seq_open(&g_zynEngine.seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
            return false;
        }
    }
    snd_seq_set_client_name(g_zynEngine.seq, "GrooveBox");
    g_zynEngine.outPort = snd_seq_create_simple_port(
        g_zynEngine.seq, "GrooveBox MIDI",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    return g_zynEngine.outPort >= 0;
}

static QString detectPreferredCard() {
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
}

static bool ensureZynRunning(const QString &presetName, const QString &presetPath, int sampleRate) {
    if (!g_zynEngine.proc) {
        g_zynEngine.proc = new QProcess();
    }
    if (g_zynEngine.proc->state() == QProcess::Running) {
        g_zynEngine.presetPath = presetPath;
        g_zynEngine.presetName = presetName;
        const bool allowOsc =
            !g_zynEngine.isYoshimi && qEnvironmentVariable("GROOVEBOX_ZYN_DISABLE_OSC") != "1";
        if (g_zynEngine.ready && !presetPath.isEmpty() &&
            g_zynEngine.lastLoadedPath != presetPath && allowOsc) {
            sendZynOscLoad(presetPath);
            g_zynEngine.lastLoadedPath = presetPath;
        }
        return g_zynEngine.ready;
    }

    if (g_zynEngine.proc->state() != QProcess::NotRunning) {
        g_zynEngine.proc->kill();
        g_zynEngine.proc->waitForFinished(2000);
    }

    const QString killOthers = qEnvironmentVariable("GROOVEBOX_ZYN_KILL_OTHERS");
    if (killOthers.isEmpty() || killOthers != "0") {
        QProcess::execute("killall", QStringList() << "-q" << "yoshimi");
        QProcess::execute("killall", QStringList() << "-q" << "zynaddsubfx");
    }

    QString exe = qEnvironmentVariable("GROOVEBOX_YOSHIMI_EXE");
    if (exe.isEmpty()) {
        exe = QStandardPaths::findExecutable("yoshimi");
    }
    bool useYoshimi = !exe.isEmpty();
    if (!useYoshimi) {
        exe = qEnvironmentVariable("GROOVEBOX_ZYN_EXE");
        if (exe.isEmpty()) {
            exe = QStandardPaths::findExecutable("zynaddsubfx");
        }
        if (exe.isEmpty()) {
            exe = "/usr/bin/zynaddsubfx";
        }
    }
    g_zynEngine.isYoshimi = useYoshimi;
    g_zynEngine.engineLabel = useYoshimi ? QString("YOSHIMI") : QString("ZYN");

    QStringList args;
    args << "--no-gui";
    if (!useYoshimi) {
        args << "-U" << "-I" << "alsa" << "-O" << "alsa";
    }
    if (sampleRate > 0) {
        args << "-r" << QString::number(sampleRate);
    }
    int buffer = qEnvironmentVariableIntValue("GROOVEBOX_YOSHIMI_BUFFER");
    if (buffer <= 0) {
        buffer = qEnvironmentVariableIntValue("GROOVEBOX_ZYN_BUFFER");
    }
    if (buffer <= 0) {
        buffer = 4096;
    }
    args << "-b" << QString::number(buffer);
    int preferredPort = qEnvironmentVariableIntValue("GROOVEBOX_YOSHIMI_OSC_PORT");
    if (preferredPort <= 0) {
        preferredPort = qEnvironmentVariableIntValue("GROOVEBOX_ZYN_OSC_PORT");
    }
    if (preferredPort <= 0) {
        preferredPort = 12221;
    }
    args << "-P" << QString::number(preferredPort);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!useYoshimi) {
        QString device = qEnvironmentVariable("GROOVEBOX_ALSA_DEVICE");
        if (device.isEmpty()) {
            const QString card = detectPreferredCard();
            if (!card.isEmpty()) {
                device = QString("hw:%1,0").arg(card);
            }
        }
        if (!device.isEmpty()) {
            QString dev = device.trimmed();
            const int colon = dev.indexOf(':');
            if (colon >= 0) {
                dev = dev.mid(colon + 1);
            }
            QRegularExpression re("(\\d+)(?:,(\\d+))?");
            const QRegularExpressionMatch m = re.match(dev);
            if (m.hasMatch()) {
                const QString card = m.captured(1);
                const QString sub = m.captured(2).isEmpty() ? QString("0") : m.captured(2);
                env.insert("ALSA_CARD", card);
                env.insert("ALSA_CTL_CARD", card);
                env.insert("ALSA_PCM_CARD", card);
                const QString pcmDevice = qEnvironmentVariable("GROOVEBOX_ZYN_PCM_DEVICE");
                env.insert("ALSA_PCM_DEVICE", pcmDevice.isEmpty() ? sub : pcmDevice);
                if (!sub.isEmpty()) {
                    env.insert("ALSA_PCM_SUBDEVICE", sub);
                }
            }
        }
    }
    g_zynEngine.proc->setProcessEnvironment(env);
    g_zynEngine.oscPort = preferredPort;
    g_zynEngine.stdoutBuffer.clear();
    g_zynEngine.ready = false;
    g_zynEngine.lastLoadedPath.clear();
    QObject::disconnect(g_zynEngine.proc, nullptr, nullptr, nullptr);
    g_zynEngine.proc->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(g_zynEngine.proc, &QProcess::readyReadStandardOutput, []() {
        parseZynOutput(g_zynEngine.proc->readAllStandardOutput());
    });
    QObject::connect(g_zynEngine.proc, &QProcess::readyReadStandardError, []() {
        parseZynOutput(g_zynEngine.proc->readAllStandardError());
    });
    if (!exe.isEmpty()) {
        g_zynEngine.proc->start(exe, args);
        if (!g_zynEngine.proc->waitForStarted(3000)) {
            g_zynEngine.proc->kill();
            g_zynEngine.proc->waitForFinished(1000);
            QProcess::startDetached(exe, args);
        }
    }

    g_zynEngine.presetPath = presetPath;
    g_zynEngine.presetName = presetName;
    g_zynEngine.destClient = -1;
    g_zynEngine.destPort = -1;

#ifdef GROOVEBOX_WITH_JACK
    if (ensureJackMidiReady()) {
        const bool connectAudio =
            qEnvironmentVariable("GROOVEBOX_JACK_CONNECT_AUDIO") != "0";
        g_zynEngine.ready = jackConnectYoshimi(connectAudio);
        const bool allowOsc =
            !g_zynEngine.isYoshimi && qEnvironmentVariable("GROOVEBOX_ZYN_DISABLE_OSC") != "1";
        if (g_zynEngine.ready && !presetPath.isEmpty() && allowOsc) {
            sendZynOscLoad(presetPath);
            g_zynEngine.lastLoadedPath = presetPath;
        }
        return g_zynEngine.ready;
    }
#endif

#ifdef GROOVEBOX_WITH_JACK
    if (qEnvironmentVariable("GROOVEBOX_ALLOW_ALSA_SEQ") != "1") {
        return false;
    }
#endif

    if (!ensureZynMidiReady()) {
        return false;
    }

    for (int i = 0; i < 35; ++i) {
        if (findZynPort(g_zynEngine.seq, g_zynEngine.destClient, g_zynEngine.destPort)) {
            break;
        }
        QThread::msleep(80);
    }
    if (g_zynEngine.destClient >= 0 && g_zynEngine.outPort >= 0) {
        snd_seq_connect_to(g_zynEngine.seq, g_zynEngine.outPort,
                           g_zynEngine.destClient, g_zynEngine.destPort);
    }
    if (g_zynEngine.destClient < 0) {
        tryAconnectZyn();
    }
    g_zynEngine.ready = (g_zynEngine.destClient >= 0 && g_zynEngine.destPort >= 0);
    const bool allowOsc =
        !g_zynEngine.isYoshimi && qEnvironmentVariable("GROOVEBOX_ZYN_DISABLE_OSC") != "1";
    if (g_zynEngine.ready && !presetPath.isEmpty() && allowOsc) {
        sendZynOscLoad(presetPath);
        sendZynOscEnablePart(0, 1);
        sendZynOscPartVolume(0, 127);
        g_zynEngine.lastLoadedPath = presetPath;
    }
    return g_zynEngine.ready;
}

static void sendZynNote(int note, int vel, bool on) {
#ifdef GROOVEBOX_WITH_JACK
    if (g_jackMidi.active) {
        jackSendNote(note, vel, on);
        return;
    }
#endif
    if (!g_zynEngine.seq || g_zynEngine.outPort < 0 ||
        g_zynEngine.destClient < 0 || g_zynEngine.destPort < 0) {
        return;
    }
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, g_zynEngine.outPort);
    snd_seq_ev_set_dest(&ev, g_zynEngine.destClient, g_zynEngine.destPort);
    snd_seq_ev_set_direct(&ev);
    if (on) {
        snd_seq_ev_set_noteon(&ev, 0, note, vel);
    } else {
        snd_seq_ev_set_noteoff(&ev, 0, note, vel);
    }
    snd_seq_event_output_direct(g_zynEngine.seq, &ev);
}

static void pollZynState() {
    if (!g_zynEngine.proc || g_zynEngine.proc->state() != QProcess::Running) {
        g_zynEngine.ready = false;
        return;
    }
#ifdef GROOVEBOX_WITH_JACK
    if (g_jackMidi.active || ensureJackMidiReady()) {
        const bool connectAudio =
            qEnvironmentVariable("GROOVEBOX_JACK_CONNECT_AUDIO") != "0";
        if (!g_jackMidi.connected) {
            jackConnectYoshimi(connectAudio);
        }
        g_zynEngine.ready = g_jackMidi.connected;
        const bool allowOsc =
            !g_zynEngine.isYoshimi && qEnvironmentVariable("GROOVEBOX_ZYN_DISABLE_OSC") != "1";
        if (g_zynEngine.ready && !g_zynEngine.presetPath.isEmpty() &&
            g_zynEngine.lastLoadedPath != g_zynEngine.presetPath && allowOsc) {
            sendZynOscLoad(g_zynEngine.presetPath);
            g_zynEngine.lastLoadedPath = g_zynEngine.presetPath;
        }
        if (g_zynEngine.ready && g_zynEngine.pendingNote >= 0) {
            const int note = g_zynEngine.pendingNote;
            const int vel = g_zynEngine.pendingVelocity;
            const int lengthMs = g_zynEngine.pendingLengthMs;
            g_zynEngine.pendingNote = -1;
            g_zynEngine.pendingVelocity = 0;
            g_zynEngine.pendingLengthMs = 0;
            sendZynNote(note, vel, true);
            if (lengthMs > 0) {
                QTimer::singleShot(lengthMs, [note]() { sendZynNote(note, 0, false); });
            }
        }
        return;
    }
#endif

#ifdef GROOVEBOX_WITH_JACK
    if (qEnvironmentVariable("GROOVEBOX_ALLOW_ALSA_SEQ") != "1") {
        g_zynEngine.ready = false;
        return;
    }
#endif
    if (!ensureZynMidiReady()) {
        return;
    }
    if (g_zynEngine.destClient < 0 || g_zynEngine.destPort < 0) {
        if (findZynPort(g_zynEngine.seq, g_zynEngine.destClient, g_zynEngine.destPort)) {
            if (g_zynEngine.outPort >= 0) {
                snd_seq_connect_to(g_zynEngine.seq, g_zynEngine.outPort,
                                   g_zynEngine.destClient, g_zynEngine.destPort);
            }
        }
    }
    g_zynEngine.ready = (g_zynEngine.destClient >= 0 && g_zynEngine.destPort >= 0);
    const bool allowOsc =
        !g_zynEngine.isYoshimi && qEnvironmentVariable("GROOVEBOX_ZYN_DISABLE_OSC") != "1";
    if (g_zynEngine.ready && !g_zynEngine.presetPath.isEmpty() &&
        g_zynEngine.lastLoadedPath != g_zynEngine.presetPath && allowOsc) {
        sendZynOscLoad(g_zynEngine.presetPath);
        sendZynOscEnablePart(0, 1);
        sendZynOscPartVolume(0, 127);
        g_zynEngine.lastLoadedPath = g_zynEngine.presetPath;
    }
    if (g_zynEngine.ready && g_zynEngine.pendingNote >= 0) {
        const int note = g_zynEngine.pendingNote;
        const int vel = g_zynEngine.pendingVelocity;
        const int lengthMs = g_zynEngine.pendingLengthMs;
        g_zynEngine.pendingNote = -1;
        g_zynEngine.pendingVelocity = 0;
        g_zynEngine.pendingLengthMs = 0;
        sendZynNote(note, vel, true);
        if (lengthMs > 0) {
            QTimer::singleShot(lengthMs, [note]() { sendZynNote(note, 0, false); });
        }
    }
}
#endif

#ifdef GROOVEBOX_WITH_ALSA
static int parseAlsaClientId(const QString &text, const QString &token) {
    const QStringList lines = text.split('\n');
    for (const QString &line : lines) {
        if (!line.contains(token, Qt::CaseInsensitive)) {
            continue;
        }
        // line format: "client 129: 'ZynAddSubFX' [type=user,pid=...]"
        const int idx = line.indexOf("client");
        if (idx < 0) {
            continue;
        }
        const int colon = line.indexOf(':', idx);
        if (colon < 0) {
            continue;
        }
        const QString num = line.mid(idx + 6, colon - (idx + 6)).trimmed();
        bool ok = false;
        const int id = num.toInt(&ok);
        if (ok) {
            return id;
        }
    }
    return -1;
}

static void tryAconnectZyn() {
#ifdef Q_OS_LINUX
    QProcess list;
    list.start("aconnect", QStringList() << "-l");
    if (!list.waitForFinished(1500)) {
        return;
    }
    const QString out = QString::fromUtf8(list.readAllStandardOutput());
    int zynClient = parseAlsaClientId(out, "Yoshimi");
    if (zynClient < 0) {
        zynClient = parseAlsaClientId(out, "ZynAddSubFX");
    }
    const int gbClient = parseAlsaClientId(out, "GrooveBox");
    if (zynClient < 0 || gbClient < 0) {
        return;
    }
    QProcess::startDetached("aconnect",
                            QStringList() << QString("%1:0").arg(gbClient)
                                          << QString("%1:0").arg(zynClient));
#endif
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

const SynthPresetInfo *findSynthPreset(const QString &name) {
    const QString key = synthPresetFromName(name).trimmed().toUpper();
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
                                                      int sampleRate, int midiNote) {
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
    fluid_synth_noteon(ctx->synth, 0, midiNote, 96);

    fluid_synth_write_float(ctx->synth, noteFrames,
                            left.data(), 0, 1, right.data(), 0, 1);
    fluid_synth_noteoff(ctx->synth, 0, midiNote);
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
#endif // GROOVEBOX_WITH_FLUIDSYNTH
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

#ifdef GROOVEBOX_WITH_ALSA
  #ifdef GROOVEBOX_WITH_JACK
    ensureJackMidiReady();
  #else
    ensureZynMidiReady();
  #endif
    scanZynPresets();
    if (hasZyn()) {
        QString preset;
        QString presetPath;
        if (!g_zynPresets.isEmpty()) {
            preset = g_zynPresets.first().display;
            presetPath = g_zynPresets.first().path;
        } else {
            preset = defaultZynLikeType();
        }
        ensureZynRunning(preset, presetPath, m_engineRate);
    }
    m_zynConnectTimer = new QTimer(this);
    m_zynConnectTimer->setInterval(300);
    connect(m_zynConnectTimer, &QTimer::timeout, this, [this]() {
    #ifdef GROOVEBOX_WITH_JACK
        if (!g_jackMidi.active) {
            ensureJackMidiReady();
        }
    #else
        ensureZynMidiReady();
    #endif
        pollZynState();
        int padIndex = -1;
        for (int i = 0; i < kPadCount; ++i) {
            if (m_isSynth[static_cast<size_t>(i)] &&
                isZynLikeType(synthTypeFromName(m_synthNames[static_cast<size_t>(i)]))) {
                padIndex = i;
                break;
            }
        }
        if (padIndex < 0) {
            return;
        }
        const QString preset = synthPresetFromName(m_synthNames[static_cast<size_t>(padIndex)]);
        const QString presetPath = zynPathForPreset(preset);
        ensureZynRunning(preset, presetPath, m_engineRate);
        pollZynState();
    });
    m_zynConnectTimer->start();
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
        if (PadRuntime *rt = m_runtime[static_cast<size_t>(to)]) {
            rebuildSynthRuntime(rt, m_synthNames[static_cast<size_t>(to)], m_engineRate,
                                m_synthBaseMidi[static_cast<size_t>(to)],
                                m_synthParams[static_cast<size_t>(to)]);
        }
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
    return preset.isEmpty() ? defaultZynLikeType() : preset;
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
    if (!name.contains(":")) {
        synthName = makeSynthName(defaultZynLikeType(),
                                  QString::fromLatin1(kSynthPresets[0].name));
    }

    m_isSynth[static_cast<size_t>(index)] = true;
    m_synthNames[static_cast<size_t>(index)] = synthName;
    m_paths[static_cast<size_t>(index)].clear();
    const SynthPresetInfo *preset = findSynthPreset(synthName);
    if (preset) {
        m_synthBaseMidi[static_cast<size_t>(index)] = preset->note;
    } else {
        m_synthBaseMidi[static_cast<size_t>(index)] = 60;
    }

    PadRuntime *rt = m_runtime[static_cast<size_t>(index)];
    if (rt) {
        if (isZynLikeType(type)) {
            rt->rawBuffer.reset();
            rt->processedBuffer.reset();
            rt->processedReady = false;
            rt->pendingProcessed = false;
            rt->rawPath = QString("synth:%1").arg(synthName);
            rt->rawDurationMs = 0;
            rt->durationMs = 0;
            rt->normalizeGain = 1.0f;
#ifdef GROOVEBOX_WITH_ALSA
            const QString preset = synthPresetFromName(synthName);
            const QString presetPath = zynPathForPreset(preset);
            ensureZynRunning(preset, presetPath, m_engineRate);
#endif
        } else {
            rebuildSynthRuntime(rt, m_synthNames[static_cast<size_t>(index)], m_engineRate,
                                m_synthBaseMidi[static_cast<size_t>(index)],
                                m_synthParams[static_cast<size_t>(index)]);
            rt->normalizeGain = 1.0f;
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
        if (isZynLikeType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
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

    if (synthPad && isZynLikeType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
#ifdef GROOVEBOX_WITH_ALSA
        const SynthParams &sp = m_synthParams[static_cast<size_t>(index)];
        const QString presetName = synthPresetFromName(m_synthNames[static_cast<size_t>(index)]);
        const QString presetPath = zynPathForPreset(presetName);
        if (ensureZynRunning(presetName, presetPath, m_engineRate)) {
            const int baseMidi = m_synthBaseMidi[static_cast<size_t>(index)];
            const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
            sendZynNote(baseMidi, velocity, true);
            const int lengthMs =
                qBound(80, static_cast<int>(300 + sp.release * 900.0f), 2000);
            QTimer::singleShot(lengthMs, this, [baseMidi]() { sendZynNote(baseMidi, 0, false); });
            return;
        }
        const int baseMidi = m_synthBaseMidi[static_cast<size_t>(index)];
        const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
        const int lengthMs =
            qBound(80, static_cast<int>(300 + sp.release * 900.0f), 2000);
        queueZynNote(baseMidi, velocity, lengthMs);
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
    if (isZynLikeType(synthTypeFromName(m_synthNames[static_cast<size_t>(index)]))) {
#ifdef GROOVEBOX_WITH_ALSA
        const QString presetName = synthPresetFromName(m_synthNames[static_cast<size_t>(index)]);
        const QString presetPath = zynPathForPreset(presetName);
        if (ensureZynRunning(presetName, presetPath, m_engineRate)) {
            PadParams &params = m_params[static_cast<size_t>(index)];
            const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
            sendZynNote(midiNote, velocity, true);
            const int bpm = m_bpm;
            const int stepMs = 60000 / qMax(1, bpm) / 4;
            const int steps = qMax(1, lengthSteps);
            const int lengthMs = qBound(60, steps * stepMs, 4000);
            QTimer::singleShot(lengthMs, this, [midiNote]() { sendZynNote(midiNote, 0, false); });
            return;
        }
        PadParams &params = m_params[static_cast<size_t>(index)];
        const int velocity = qBound(1, static_cast<int>(params.volume * 127.0f), 127);
        const int bpm = m_bpm;
        const int stepMs = 60000 / qMax(1, bpm) / 4;
        const int steps = qMax(1, lengthSteps);
        const int lengthMs = qBound(60, steps * stepMs, 4000);
        queueZynNote(midiNote, velocity, lengthMs);
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
    scanZynPresets();
    QStringList list;
    for (const auto &preset : g_zynPresets) {
        list << preset.display;
    }
    if (!list.isEmpty()) {
        return list;
    }
    for (const auto &preset : kSynthPresets) {
        list << QString::fromLatin1(preset.name);
    }
    return list;
}

QStringList PadBank::serumWaves() {
    return {"SINE", "SAW", "SQUARE", "TRI", "NOISE"};
}

QStringList PadBank::synthTypes() {
    return {defaultZynLikeType()};
}

bool PadBank::hasFluidSynth() {
#ifdef GROOVEBOX_WITH_FLUIDSYNTH
    return true;
#else
    return false;
#endif
}

bool PadBank::hasZyn() {
#ifdef Q_OS_LINUX
    if (!QStandardPaths::findExecutable("yoshimi").isEmpty()) {
        return true;
    }
    return !QStandardPaths::findExecutable("zynaddsubfx").isEmpty();
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
