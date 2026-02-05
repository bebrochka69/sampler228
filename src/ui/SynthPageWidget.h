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
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    enum Mode {
        ModeFluid,
        ModeSerum
    };

    struct PresetRow {
        QString label;
        QString presetId;
        bool header = false;
        QRectF rect;
    };

    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    int m_selectedParam = 0;
    int m_dragParam = -1;
    Mode m_mode = ModeFluid;
    QStringList m_fluidPresets;
    QVector<PresetRow> m_presetRows;
    QVector<QRectF> m_adsrRects;
};
