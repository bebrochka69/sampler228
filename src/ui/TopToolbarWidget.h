#pragma once

#include <QPolygonF>
#include <QStringList>
#include <QRectF>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "SystemStats.h"

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;
class PadBank;

class TopToolbarWidget : public QWidget {
    Q_OBJECT
public:
    explicit TopToolbarWidget(PadBank *pads, QWidget *parent = nullptr);

    int activeIndex() const { return m_activeIndex; }

public slots:
    void setActiveIndex(int index);

signals:
    void pageSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void rebuildTabs();
    void updateStats();
    void adjustBpm(int delta);

    QStringList m_tabs;
    QVector<QPolygonF> m_tabPolys;
    int m_tabsWidth = 0;
    int m_activeIndex = 0;

    SystemStats m_stats;
    QTimer m_statsTimer;
    float m_levelL = 0.0f;
    float m_levelR = 0.0f;

    PadBank *m_pads = nullptr;
    QVector<QRectF> m_padRects;
    QRectF m_bpmRect;
    int m_bpm = 120;
};
