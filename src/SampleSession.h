#pragma once

#include <QAudioDecoder>
#include <QMediaPlayer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QElapsedTimer>
#include <QTimer>

class QAudioOutput;
class PadBank;

class SampleSession : public QObject {
    Q_OBJECT
public:
    explicit SampleSession(PadBank *pads = nullptr, QObject *parent = nullptr);
    void setPreviewPads(PadBank *pads) { m_pads = pads; }

    enum class DecodeMode {
        Full,
        Fast,
        None
    };

    void setSource(const QString &path, DecodeMode mode = DecodeMode::Full);
    QString sourcePath() const { return m_sourcePath; }
    DecodeMode decodeMode() const { return m_decodeMode; }

    void play();
    void stop();
    bool isPlaying() const;
    float playbackProgress() const;

    const QVector<float> &waveform() const { return m_waveform; }
    bool hasWaveform() const { return !m_waveform.isEmpty(); }
    QString infoText() const { return m_infoText; }
    QString errorText() const { return m_errorText; }

signals:
    void waveformChanged();
    void infoChanged();
    void playbackChanged(bool playing);
    void errorChanged(const QString &message);

private slots:
    void handleBufferReady();
    void handleDecodeFinished();
    void handleDecodeError(QAudioDecoder::Error error);
    void handlePlayerState(QMediaPlayer::PlaybackState state);

private:
    void startDecode();
    void resetDecodeState();
    void rebuildWaveform();
    void ensureAudioOutput();
    void playExternal();
    void stopExternal();
    bool buildExternalCommand(QString &program, QStringList &args) const;
    bool playPreviewViaEngine();
    void stopPreview();

    QString m_sourcePath;
    DecodeMode m_decodeMode = DecodeMode::Full;
    QAudioDecoder m_decoder;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    bool m_hasAudioOutput = false;
    bool m_forceExternal = false;
    QProcess *m_externalPlayer = nullptr;
    PadBank *m_pads = nullptr;
    bool m_previewActive = false;
    qint64 m_previewDurationMs = 0;
    QElapsedTimer m_previewTimer;
    QTimer m_previewPoll;

    QVector<float> m_pcm;
    QVector<float> m_waveform;
    int m_sampleRate = 0;
    int m_channels = 0;
    qint64 m_frames = 0;
    qint64 m_durationMs = 0;
    qint64 m_playbackPosMs = 0;
    QString m_infoText;
    QString m_errorText;
    bool m_decoding = false;
};
