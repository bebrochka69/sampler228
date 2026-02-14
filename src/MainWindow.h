#pragma once

#include <QMainWindow>

class QStackedWidget;
class TopToolbarWidget;
class SampleSession;
class PadBank;
class PadAssignOverlay;
class PadHoldMenuOverlay;
class PianoRollOverlay;
class SynthPageWidget;
class SeqPageWidget;
class FxPageWidget;
class ProjectMenuOverlay;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    TopToolbarWidget *m_toolbar = nullptr;
    QStackedWidget *m_stack = nullptr;
    SampleSession *m_sampleSession = nullptr;
    PadBank *m_padBank = nullptr;
    PadAssignOverlay *m_assignOverlay = nullptr;
    PadHoldMenuOverlay *m_holdMenu = nullptr;
    PianoRollOverlay *m_pianoRoll = nullptr;
    SynthPageWidget *m_synthPage = nullptr;
    SeqPageWidget *m_seqPage = nullptr;
    FxPageWidget *m_fxPage = nullptr;
    ProjectMenuOverlay *m_projectMenu = nullptr;
};
