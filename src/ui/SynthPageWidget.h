#pragma once

#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QPaintEvent;
class QMouseEvent;
class QKeyEvent;
class PadBank;

class SynthPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SynthPageWidget(PadBank *pads, QWidget *parent = nullptr);

    void setActivePad(int pad);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    int m_selectedParam = 0;
    QStringList m_waveforms;
    QVector<QRectF> m_paramRects;
};
