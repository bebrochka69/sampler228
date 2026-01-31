#pragma once

#include <QRectF>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

#include "SampleBrowserModel.h"

class QMouseEvent;
class QPaintEvent;
class QKeyEvent;
class QWheelEvent;
class SampleSession;
class PadBank;

class SamplePageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SamplePageWidget(SampleSession *session, PadBank *pads, QWidget *parent = nullptr);
    void setAssignMode(bool enabled) { m_assignMode = enabled; }

signals:
    void sampleAssigned();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void refreshBrowser();
    void rebuildProjects();
    void clampScroll();
    void selectIndex(int index);
    int indexOfNode(SampleBrowserModel::Node *node) const;
    QString currentDirLabel() const;

    SampleSession *m_session = nullptr;
    PadBank *m_pads = nullptr;
    bool m_assignMode = false;
    SampleBrowserModel m_browser;
    QVector<SampleBrowserModel::Entry> m_entries;
    QStringList m_projects;
    int m_scrollOffset = 0;
    int m_selectedIndex = -1;

    QRectF m_playRect;
    QRectF m_stopRect;
    QRectF m_rescanRect;

    QTimer m_ambientTimer;
};
