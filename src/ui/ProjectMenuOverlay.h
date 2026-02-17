#pragma once

#include <QWidget>
#include <QRectF>
#include <QStringList>
#include <QVector>

class PadBank;
class SeqPageWidget;
class FxPageWidget;

class ProjectMenuOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ProjectMenuOverlay(PadBank *pads, SeqPageWidget *seq, FxPageWidget *fx,
                                QWidget *parent = nullptr);

    void showMenu();

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void refreshProjects();
    QString mediaRoot() const;
    QString projectDir() const;
    QString renderDir() const;
    void ensureMediaDirs();
    void openBluetoothMenu();

    void newProject();
    bool saveProject(const QString &name);
    bool loadProject(const QString &name);
    void renderProject();

    PadBank *m_pads = nullptr;
    SeqPageWidget *m_seq = nullptr;
    FxPageWidget *m_fx = nullptr;

    QStringList m_projectNames;
    int m_selectedProject = -1;

    int m_renderBars = 4;
    int m_renderRate = 44100;
    bool m_metronome = false;

    QRectF m_panelRect;
    QRectF m_leftRect;
    QRectF m_rightTopRect;
    QRectF m_rightBottomRect;
    QRectF m_closeRect;

    QRectF m_bpmMinusRect;
    QRectF m_bpmPlusRect;
    QRectF m_metronomeRect;
    QRectF m_rateRect;
    QRectF m_bluetoothRect;

    QRectF m_newRect;
    QRectF m_saveRect;
    QRectF m_loadRect;
    QVector<QRectF> m_projectRowRects;

    QRectF m_renderBarsRect;
    QRectF m_renderBtnRect;
};
