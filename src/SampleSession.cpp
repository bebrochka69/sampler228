#include "SampleSession.h"

#include <QAudioBuffer>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QStandardPaths>
#include <QTime>
#include <QUrl>
#include <QtGlobal>
#include <QtMath>

namespace {
float sampleToFloat(const char *data, QAudioFormat::SampleFormat format) {
    switch (format) {
        case QAudioFormat::UInt8: {
            const quint8 v = *reinterpret_cast<const quint8 *>(data);
            return (static_cast<float>(v) - 128.0f) / 128.0f;
        }
        case QAudioFormat::Int16: {
            const qint16 v = *reinterpret_cast<const qint16 *>(data);
            return static_cast<float>(v) / 32768.0f;
        }
        case QAudioFormat::Int32: {
            const qint32 v = *reinterpret_cast<const qint32 *>(data);
            return static_cast<float>(v) / 2147483648.0f;
        }
        case QAudioFormat::Float: {
            const float v = *reinterpret_cast<const float *>(data);
            return v;
        }
        default:
            return 0.0f;
    }
}
}  // namespace

SampleSession::SampleSession(QObject *parent) : QObject(parent) {
    connect(&m_decoder, &QAudioDecoder::bufferReady, this, &SampleSession::handleBufferReady);
    connect(&m_decoder, &QAudioDecoder::finished, this, &SampleSession::handleDecodeFinished);
    connect(&m_decoder,
            static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(&QAudioDecoder::error),
            this, &SampleSession::handleDecodeError);

#ifdef Q_OS_LINUX
    const QString platform = QGuiApplication::platformName();
    if (platform.contains("linuxfb") || platform.contains("eglfs") ||
        platform.contains("vkkhrdisplay")) {
        m_forceExternal = true;
    }
    if (qEnvironmentVariableIsSet("GROOVEBOX_FORCE_ALSA")) {
        m_forceExternal = true;
    }
#endif

    ensureAudioOutput();
}

void SampleSession::setSource(const QString &path) {
    if (m_sourcePath == path) {
        return;
    }

    m_sourcePath = path;
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl::fromLocalFile(path));
    }
    stopExternal();
    resetDecodeState();
    m_errorText.clear();

    if (!m_hasAudioOutput && !m_forceExternal) {
        m_errorText = "No audio output device";
        emit errorChanged(m_errorText);
    }

    if (!m_sourcePath.isEmpty()) {
        m_infoText = "Loading...";
        emit infoChanged();
        startDecode();
    }
}

void SampleSession::play() {
    if (m_sourcePath.isEmpty()) {
        return;
    }
    if (m_forceExternal) {
        playExternal();
        return;
    }
    ensureAudioOutput();
    if (m_forceExternal || !m_player || !m_hasAudioOutput) {
        if (m_errorText != "No audio output device") {
            m_errorText = "No audio output device";
            emit errorChanged(m_errorText);
        }
        playExternal();
        return;
    }
    if (m_player->source().toLocalFile() != m_sourcePath) {
        m_player->setSource(QUrl::fromLocalFile(m_sourcePath));
    }
    m_player->play();
}

void SampleSession::stop() {
    if (m_player) {
        m_player->stop();
    }
    stopExternal();
}

bool SampleSession::isPlaying() const {
    if (m_externalPlayer) {
        return m_externalPlayer->state() == QProcess::Running;
    }
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

void SampleSession::startDecode() {
    m_decoder.stop();
    m_decoding = true;
    m_decoder.setSource(QUrl::fromLocalFile(m_sourcePath));
    m_decoder.start();
}

void SampleSession::resetDecodeState() {
    m_decoder.stop();
    m_decoding = false;
    m_pcm.clear();
    m_waveform.clear();
    m_sampleRate = 0;
    m_channels = 0;
    m_frames = 0;
    m_infoText = "No sample selected";
    emit waveformChanged();
    emit infoChanged();
}

void SampleSession::handleBufferReady() {
    const QAudioBuffer buffer = m_decoder.read();
    if (!buffer.isValid()) {
        return;
    }

    const QAudioFormat format = buffer.format();
    if (format.sampleRate() == 0 || format.channelCount() == 0) {
        return;
    }

    if (m_sampleRate == 0) {
        m_sampleRate = format.sampleRate();
        m_channels = format.channelCount();
    }

    const int channelCount = format.channelCount();
    const int bytesPerSample = format.bytesPerSample();
    const int bytesPerFrame = bytesPerSample * channelCount;
    const int frames = buffer.frameCount();

    const char *data = buffer.constData<char>();
    for (int frame = 0; frame < frames; ++frame) {
        const char *framePtr = data + frame * bytesPerFrame;
        float maxValue = 0.0f;
        for (int channel = 0; channel < channelCount; ++channel) {
            const char *samplePtr = framePtr + channel * bytesPerSample;
            const float v = qAbs(sampleToFloat(samplePtr, format.sampleFormat()));
            if (v > maxValue) {
                maxValue = v;
            }
        }
        m_pcm.push_back(maxValue);
    }

    m_frames += buffer.frameCount();
}

void SampleSession::handleDecodeFinished() {
    m_decoding = false;
    rebuildWaveform();
}

void SampleSession::handleDecodeError(QAudioDecoder::Error error) {
    Q_UNUSED(error);
    m_decoding = false;
    m_errorText = m_decoder.errorString();
    emit errorChanged(m_errorText);
}

void SampleSession::handlePlayerState(QMediaPlayer::PlaybackState state) {
    emit playbackChanged(state == QMediaPlayer::PlayingState);
}

void SampleSession::ensureAudioOutput() {
    if (m_player) {
        return;
    }
    if (m_forceExternal) {
        return;
    }

    const QAudioDevice output = QMediaDevices::defaultAudioOutput();
    if (output.isNull()) {
        m_hasAudioOutput = false;
        if (m_errorText != "No audio output device") {
            m_errorText = "No audio output device";
            emit errorChanged(m_errorText);
        }
        return;
    }

    m_hasAudioOutput = true;
    m_audioOutput = new QAudioOutput(output, this);
    m_player = new QMediaPlayer(this);
    m_player->setAudioOutput(m_audioOutput);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, &SampleSession::handlePlayerState);
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &errorString) {
                if (error == QMediaPlayer::NoError) {
                    return;
                }
                m_errorText = errorString;
                emit errorChanged(m_errorText);
                m_forceExternal = true;
                if (!m_externalPlayer) {
                    playExternal();
                }
            });

    if (m_errorText == "No audio output device") {
        m_errorText.clear();
        emit errorChanged(m_errorText);
    }
}

bool SampleSession::buildExternalCommand(QString &program, QStringList &args) const {
#ifdef Q_OS_LINUX
    const QFileInfo info(m_sourcePath);
    const QString ext = info.suffix().toLower();
    if (ext == "wav") {
        program = QStandardPaths::findExecutable("aplay");
        if (program.isEmpty()) {
            return false;
        }
        args = {"-q", m_sourcePath};
        return true;
    }

    if (ext == "mp3") {
        program = QStandardPaths::findExecutable("mpg123");
        if (!program.isEmpty()) {
            args = {"-q", m_sourcePath};
            return true;
        }

        program = QStandardPaths::findExecutable("ffplay");
        if (!program.isEmpty()) {
            args = {"-nodisp", "-autoexit", "-loglevel", "quiet", m_sourcePath};
            return true;
        }
    }
#else
    Q_UNUSED(program);
    Q_UNUSED(args);
#endif
    return false;
}

void SampleSession::playExternal() {
    if (m_sourcePath.isEmpty()) {
        return;
    }
    stopExternal();

    QString program;
    QStringList args;
    if (!buildExternalCommand(program, args)) {
        if (m_errorText != "External player not found") {
            m_errorText = "External player not found";
            emit errorChanged(m_errorText);
        }
        return;
    }

    m_externalPlayer = new QProcess(this);
    m_externalPlayer->setProgram(program);
    m_externalPlayer->setArguments(args);
    m_externalPlayer->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_externalPlayer, &QProcess::started, this, [this]() {
        if (!m_errorText.isEmpty()) {
            m_errorText.clear();
            emit errorChanged(m_errorText);
        }
        emit playbackChanged(true);
    });
    connect(m_externalPlayer,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
                emit playbackChanged(false);
                m_externalPlayer->deleteLater();
                m_externalPlayer = nullptr;
            });
    connect(m_externalPlayer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_errorText != "External player failed") {
            m_errorText = "External player failed";
            emit errorChanged(m_errorText);
        }
        emit playbackChanged(false);
    });

    m_externalPlayer->start();
}

void SampleSession::stopExternal() {
    if (!m_externalPlayer) {
        return;
    }
    m_externalPlayer->kill();
    m_externalPlayer->deleteLater();
    m_externalPlayer = nullptr;
    emit playbackChanged(false);
}

void SampleSession::rebuildWaveform() {
    m_waveform.clear();
    if (m_pcm.isEmpty() || m_sampleRate <= 0) {
        emit waveformChanged();
        return;
    }

    const int target = 240;
    m_waveform.resize(target);
    const int total = m_pcm.size();

    for (int i = 0; i < target; ++i) {
        const float pos = (static_cast<float>(i) / static_cast<float>(target - 1)) * (total - 1);
        const int idx = static_cast<int>(pos);
        const int next = qMin(idx + 1, total - 1);
        const float frac = pos - static_cast<float>(idx);
        const float value = m_pcm[idx] * (1.0f - frac) + m_pcm[next] * frac;
        m_waveform[i] = value;
    }

    const qint64 ms = (m_frames * 1000) / m_sampleRate;
    QTime time(0, 0);
    time = time.addMSecs(static_cast<int>(ms));
    const QString length = (ms >= 3600000) ? time.toString("hh:mm:ss") : time.toString("mm:ss.zzz");

    m_infoText = QString("Len %1  |  %2 Hz  |  %3 ch").arg(length).arg(m_sampleRate).arg(m_channels);

    emit waveformChanged();
    emit infoChanged();
}
