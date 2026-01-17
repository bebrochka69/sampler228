#pragma once

#include <QMainWindow>

class BpmArcWidget;
class QStackedWidget;
class TopToolbarWidget;
class QResizeEvent;
class SampleSession;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    TopToolbarWidget *m_toolbar = nullptr;
    QStackedWidget *m_stack = nullptr;
    BpmArcWidget *m_bpmArc = nullptr;
    SampleSession *m_sampleSession = nullptr;
};
