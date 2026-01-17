#pragma once

#include <array>
#include <QColor>
#include <QWidget>

class QMouseEvent;
class QPaintEvent;

class SeqPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SeqPageWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    QRectF gridRect() const;
    QRectF padsRect() const;

    std::array<std::array<bool, 64>, 8> m_steps;
    std::array<QColor, 8> m_padColors;
    int m_activePad = 0;
};
