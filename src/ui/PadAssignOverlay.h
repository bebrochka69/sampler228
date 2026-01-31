#pragma once

#include <QRectF>
#include <QVector>
#include <QWidget>

class SamplePageWidget;
class SynthPageWidget;
class PadBank;
class SampleSession;
class QStackedWidget;

class PadAssignOverlay : public QWidget {
    Q_OBJECT
public:
    explicit PadAssignOverlay(SampleSession *session, PadBank *pads, QWidget *parent = nullptr);

    void showForPad(int pad);
    void showForPad(int pad, int tabIndex);

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    void switchTab(int tab);
    void handleAssigned();

    SamplePageWidget *m_samplePage = nullptr;
    QWidget *m_synthSelect = nullptr;
    QStackedWidget *m_stack = nullptr;
    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    int m_tab = 0;
    QRectF m_samplesTab;
    QRectF m_synthTab;
    QRectF m_closeRect;
};
