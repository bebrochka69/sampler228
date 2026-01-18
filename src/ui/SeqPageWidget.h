#pragma once

#include <array>
#include <QColor>
#include <QTimer>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;
class QKeyEvent;

class SeqPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SeqPageWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRectF gridRect() const;
    QRectF padsRect() const;
    int stepIntervalMs() const;
    void togglePlayback();
    void advancePlayhead();

    std::array<std::array<bool, 64>, 8> m_steps;
    std::array<QColor, 8> m_padColors;
    int m_activePad = 0;
    QTimer m_playTimer;
    bool m_playing = false;
    int m_playStep = 0;
    int m_bpm = 120;
};
