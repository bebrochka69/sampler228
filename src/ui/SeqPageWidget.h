#pragma once

#include <array>
#include <QColor>
#include <QTimer>
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

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
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
    bool m_playing = false;
    bool m_waiting = false;
    int m_playStep = 0;
    int m_bpm = 120;
    PadBank *m_pads = nullptr;
};
