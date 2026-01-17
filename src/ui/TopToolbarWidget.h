#pragma once

#include <QPolygonF>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "SystemStats.h"

class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

class TopToolbarWidget : public QWidget {
    Q_OBJECT
public:
    explicit TopToolbarWidget(QWidget *parent = nullptr);

    int activeIndex() const { return m_activeIndex; }

public slots:
    void setActiveIndex(int index);

signals:
    void pageSelected(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuildTabs();
    void updateStats();
    void tickMeters();

    QStringList m_tabs;
    QVector<QPolygonF> m_tabPolys;
    int m_tabsWidth = 0;
    int m_activeIndex = 0;

    SystemStats m_stats;
    QTimer m_statsTimer;
    QTimer m_meterTimer;

    float m_levelL = 0.2f;
    float m_levelR = 0.26f;
    float m_peakL = 0.2f;
    float m_peakR = 0.26f;
    int m_clipHoldL = 0;
    int m_clipHoldR = 0;
    float m_phase = 0.0f;

    QVector<bool> m_padLoaded;
};
