#pragma once

#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "SampleBrowserModel.h"

class QMouseEvent;
class QPaintEvent;
class QWheelEvent;
class SampleSession;

class SamplePageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SamplePageWidget(SampleSession *session, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void refreshBrowser();
    void rebuildProjects();
    void clampScroll();

    SampleSession *m_session = nullptr;
    SampleBrowserModel m_browser;
    QVector<SampleBrowserModel::Entry> m_entries;
    QStringList m_projects;
    int m_scrollOffset = 0;

    QRectF m_playRect;
    QRectF m_stopRect;
    QRectF m_rescanRect;
};
