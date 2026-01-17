#pragma once

#include <QMainWindow>

class QStackedWidget;
class TopToolbarWidget;
class SampleSession;
class PadBank;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    TopToolbarWidget *m_toolbar = nullptr;
    QStackedWidget *m_stack = nullptr;
    SampleSession *m_sampleSession = nullptr;
    PadBank *m_padBank = nullptr;
};
