#pragma once

#include <array>
#include <QColor>
#include <QVector>
#include <QTimer>
#include <QElapsedTimer>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QKeyEvent;
class QEvent;
class QWheelEvent;
class PadBank;

class SeqPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SeqPageWidget(PadBank *pads, QWidget *parent = nullptr);
    void applyPianoSteps(int pad, const QVector<int> &steps);

signals:
    void padOpenRequested(int pad);
    void padAssignRequested(int pad);
    void padMenuRequested(int pad);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRectF gridRect() const;
    int stepIntervalMs() const;
    bool padsReady() const;
    void startPlayback();
    void togglePlayback();
    void advancePlayhead();
    void triggerStep(int step);

    std::array<std::array<bool, 64>, 8> m_steps;
    std::array<QColor, 8> m_padColors;
    int m_activePad = 0;
    QTimer m_playTimer;
    QTimer m_readyTimer;
    QTimer m_animTimer;
    QTimer m_longPressTimer;
    QElapsedTimer m_playClock;
    qint64 m_lastStepMs = 0;
    bool m_playing = false;
    bool m_waiting = false;
    bool m_longPressTriggered = false;
    bool m_pressOnLabel = false;
    bool m_scrubActive = false;
    int m_playStep = 0;
    int m_bpm = 120;
    PadBank *m_pads = nullptr;
    QPointF m_pressPos;
    int m_pressedPad = -1;
};
