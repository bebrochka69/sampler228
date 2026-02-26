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

#include "PadBank.h"

namespace {
constexpr int kFastPcmLimit = 6000;

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

SampleSession::SampleSession(PadBank *pads, QObject *parent) : QObject(parent), m_pads(pads) {
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

    m_previewPoll.setInterval(150);
    connect(&m_previewPoll, &QTimer::timeout, this, [this]() {
        if (!m_previewActive) {
            m_previewPoll.stop();
            return;
        }
        if (m_pads && m_pads->isPreviewActive()) {
            return;
        }
        m_previewActive = false;
        m_previewDurationMs = 0;
        emit playbackChanged(false);
        m_previewPoll.stop();
    });

    ensureAudioOutput();
}

void SampleSession::setSource(const QString &path, DecodeMode mode) {
    if (m_sourcePath == path && m_decodeMode == mode) {
        return;
    }

    m_sourcePath = path;
    m_decodeMode = mode;
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
        if (m_decodeMode == DecodeMode::None) {
            m_infoText = QFileInfo(m_sourcePath).fileName();
            emit infoChanged();
            return;
        }

        m_infoText = "Loading...";
        emit infoChanged();
        startDecode();
    }
}

void SampleSession::play() {
    if (m_sourcePath.isEmpty()) {
        return;
    }
    if (playPreviewViaEngine()) {
        return;
    }
    if (m_forceExternal) {
        playExternal();
        return;
    }
    if (m_decodeMode == DecodeMode::None) {
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
    stopPreview();
    if (m_player) {
        m_player->stop();
    }
    stopExternal();
}

bool SampleSession::isPlaying() const {
    if (m_previewActive) {
        return true;
    }
    if (m_externalPlayer) {
        return m_externalPlayer->state() == QProcess::Running;
    }
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

float SampleSession::playbackProgress() const {
    if (m_previewActive && m_previewDurationMs > 0) {
        const qint64 elapsed = m_previewTimer.isValid() ? m_previewTimer.elapsed() : 0;
        const float ratio =
            static_cast<float>(static_cast<double>(elapsed) / static_cast<double>(m_previewDurationMs));
        return qBound(0.0f, ratio, 1.0f);
    }
    if (m_durationMs <= 0) {
        return -1.0f;
    }
    const float ratio =
        static_cast<float>(static_cast<double>(m_playbackPosMs) / static_cast<double>(m_durationMs));
    return qBound(0.0f, ratio, 1.0f);
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
    m_durationMs = 0;
    m_playbackPosMs = 0;
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
    const bool fast = (m_decodeMode == DecodeMode::Fast);
    auto downsamplePcm = [this]() {
        if (m_pcm.size() < 2) {
            return;
        }
        QVector<float> compact;
        compact.reserve((m_pcm.size() + 1) / 2);
        for (int i = 0; i < m_pcm.size(); i += 2) {
            float v = m_pcm[i];
            if (i + 1 < m_pcm.size()) {
                v = qMax(v, m_pcm[i + 1]);
            }
            compact.push_back(v);
        }
        m_pcm.swap(compact);
    };

    if (fast && m_pcm.size() >= kFastPcmLimit) {
        downsamplePcm();
    }

    int stride = 1;
    if (fast) {
        const int remaining = qMax(1, kFastPcmLimit - m_pcm.size());
        stride = qMax(1, frames / remaining);
    }

    for (int frame = 0; frame < frames; frame += stride) {
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

    if (fast) {
        while (m_pcm.size() > kFastPcmLimit) {
            downsamplePcm();
        }
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
    if (state != QMediaPlayer::PlayingState) {
        m_playbackPosMs = 0;
    }
    emit playbackChanged(state == QMediaPlayer::PlayingState);
}

bool SampleSession::playPreviewViaEngine() {
    if (!m_pads) {
        return false;
    }
    if (!m_pads->previewSample(m_sourcePath, nullptr)) {
        return false;
    }
    m_previewActive = true;
    m_previewDurationMs = 0;
    m_previewTimer.restart();
    emit playbackChanged(true);
    if (!m_previewPoll.isActive()) {
        m_previewPoll.start();
    }
    return true;
}

void SampleSession::stopPreview() {
    if (!m_previewActive) {
        return;
    }
    m_previewActive = false;
    m_previewDurationMs = 0;
    m_previewPoll.stop();
    if (m_pads) {
        m_pads->stopPreview();
    }
    emit playbackChanged(false);
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
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        m_playbackPosMs = pos;
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (duration > 0) {
            m_durationMs = duration;
        }
    });
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
    const QString alsaDevice = qEnvironmentVariable("GROOVEBOX_ALSA_DEVICE");
    if (ext == "wav") {
        program = QStandardPaths::findExecutable("aplay");
        if (program.isEmpty()) {
            return false;
        }
        args = {"-q"};
        if (!alsaDevice.isEmpty()) {
            args << "-D" << alsaDevice;
        }
        args << m_sourcePath;
        return true;
    }

    if (ext == "mp3") {
        program = QStandardPaths::findExecutable("mpg123");
        if (!program.isEmpty()) {
            args = {"-q"};
            if (!alsaDevice.isEmpty()) {
                args << "-a" << alsaDevice;
            }
            args << m_sourcePath;
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
    QProcess *proc = m_externalPlayer;
    proc->setProgram(program);
    proc->setArguments(args);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    connect(proc, &QProcess::started, this, [this]() {
        m_playbackPosMs = 0;
        if (!m_errorText.isEmpty()) {
            m_errorText.clear();
            emit errorChanged(m_errorText);
        }
        emit playbackChanged(true);
    });
    connect(proc,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), this,
            [this, proc](int, QProcess::ExitStatus) {
                emit playbackChanged(false);
                if (m_externalPlayer == proc) {
                    m_externalPlayer = nullptr;
                }
                proc->deleteLater();
            });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        if (m_errorText != "External player failed") {
            m_errorText = "External player failed";
            emit errorChanged(m_errorText);
        }
        emit playbackChanged(false);
        if (m_externalPlayer == proc) {
            m_externalPlayer = nullptr;
        }
        proc->deleteLater();
    });

    proc->start();
}

void SampleSession::stopExternal() {
    if (!m_externalPlayer) {
        return;
    }
    QProcess *proc = m_externalPlayer;
    m_externalPlayer = nullptr;
    proc->disconnect(this);
    proc->kill();
    proc->deleteLater();
    m_playbackPosMs = 0;
    emit playbackChanged(false);
}

void SampleSession::rebuildWaveform() {
    m_waveform.clear();
    if (m_pcm.isEmpty() || m_sampleRate <= 0) {
        emit waveformChanged();
        return;
    }

    const int maxTarget = 1200;
    const int minTarget = 240;
    const int target = qBound(minTarget, m_pcm.size(), maxTarget);
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
    m_durationMs = ms;
    QTime time(0, 0);
    time = time.addMSecs(static_cast<int>(ms));
    const QString length = (ms >= 3600000) ? time.toString("hh:mm:ss") : time.toString("mm:ss.zzz");

    m_infoText = QString("Len %1  |  %2 Hz  |  %3 ch").arg(length).arg(m_sampleRate).arg(m_channels);

    emit waveformChanged();
    emit infoChanged();
}
