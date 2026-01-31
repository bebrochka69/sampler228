#pragma once

#include <QMainWindow>

class QStackedWidget;
class TopToolbarWidget;
class SampleSession;
class PadBank;
class PadAssignOverlay;
class PianoRollOverlay;
class SynthPageWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    TopToolbarWidget *m_toolbar = nullptr;
    QStackedWidget *m_stack = nullptr;
    SampleSession *m_sampleSession = nullptr;
    PadBank *m_padBank = nullptr;
    PadAssignOverlay *m_assignOverlay = nullptr;
    PianoRollOverlay *m_pianoRoll = nullptr;
    SynthPageWidget *m_synthPage = nullptr;
};
