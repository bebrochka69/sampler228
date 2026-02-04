#pragma once

#include <QWidget>

class PadBank;

class PadHoldMenuOverlay : public QWidget {
    Q_OBJECT
public:
    explicit PadHoldMenuOverlay(PadBank *pads, QWidget *parent = nullptr);

    void showForPad(int pad);

signals:
    void pianoRollRequested(int pad);
    void replaceRequested(int pad);
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    QRectF m_panelRect;
    QRectF m_closeRect;
    QRectF m_pianoRect;
    QRectF m_replaceRect;
    QRectF m_cancelRect;
};
