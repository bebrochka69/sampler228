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
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct ViewState {
        float zoom = 1.0f;
        float offset = 0.0f;
    };

    QRectF gridRect() const;
    QRectF listRect() const;
    QRectF gridInnerRect() const;
    int stepIntervalMs() const;
    bool padsReady() const;
    void startPlayback();
    void togglePlayback();
    void advancePlayhead();
    void triggerStep(int step);
    void clampView(int pad);
    void applyZoom(int pad, float scale, float centerRatio);
    void applyPan(int pad, float deltaSteps);

    std::array<std::array<bool, 64>, 8> m_steps;
    std::array<QColor, 8> m_padColors;
    std::array<ViewState, 8> m_views;
    int m_activePad = 0;
    QTimer m_playTimer;
    QTimer m_readyTimer;
    bool m_playing = false;
    bool m_waiting = false;
    int m_playStep = 0;
    int m_bpm = 120;
    PadBank *m_pads = nullptr;

    bool m_dragging = false;
    bool m_dragMoved = false;
    QPointF m_dragStartPos;
    float m_dragStartOffset = 0.0f;
};
