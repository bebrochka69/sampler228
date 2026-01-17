#include "SampleSession.h"

#include <QAudioBuffer>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QTime>
#include <QUrl>
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
    resetDecodeState();
    m_errorText.clear();

    if (!m_hasAudioOutput) {
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
    ensureAudioOutput();
    if (!m_player || !m_hasAudioOutput) {
        if (m_errorText != "No audio output device") {
            m_errorText = "No audio output device";
            emit errorChanged(m_errorText);
        }
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
}

bool SampleSession::isPlaying() const {
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

    if (m_errorText == "No audio output device") {
        m_errorText.clear();
        emit errorChanged(m_errorText);
    }
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
