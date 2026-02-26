#include "MainWindow.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "FramebufferCleaner.h"
#include "ui/EditPageWidget.h"
#include "ui/FxPageWidget.h"
#include "ui/PadAssignOverlay.h"
#include "ui/PadHoldMenuOverlay.h"
#include "ui/PianoRollOverlay.h"
#include "ui/ProjectMenuOverlay.h"
#include "ui/SeqPageWidget.h"
#include "ui/SimplePageWidget.h"
#include "ui/SynthPageWidget.h"
#include "ui/TopToolbarWidget.h"
#include "PadBank.h"
#include "SampleSession.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("GrooveBox UI");
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_padBank = new PadBank(this);
    m_toolbar = new TopToolbarWidget(m_padBank, central);

    m_stack = new QStackedWidget(central);
    m_sampleSession = new SampleSession(m_padBank, this);
    m_seqPage = new SeqPageWidget(m_padBank, m_stack);
    auto *editPage = new EditPageWidget(m_sampleSession, m_padBank, m_stack);
    m_fxPage = new FxPageWidget(m_padBank, m_stack);
    m_synthPage = new SynthPageWidget(m_padBank, m_stack);

    m_stack->addWidget(m_seqPage);                      // 0
    m_stack->addWidget(m_fxPage);                       // 1
    m_stack->addWidget(new SimplePageWidget("ARRANGE", m_stack)); // 2
    m_stack->addWidget(editPage);                     // 3
    m_stack->addWidget(m_synthPage);                  // 4

    layout->addWidget(m_toolbar);
    layout->addWidget(m_stack, 1);

    setCentralWidget(central);

    connect(m_toolbar, &TopToolbarWidget::pageSelected, this, [this](int tabIndex) {
        // Tabs: 0=SEQ, 1=FX, 2=ARRANGE
        if (tabIndex == 0) {
            m_stack->setCurrentIndex(0);
        } else if (tabIndex == 1) {
            m_stack->setCurrentIndex(1);
        } else if (tabIndex == 2) {
            m_stack->setCurrentIndex(2);
        }
    });
    connect(m_stack, &QStackedWidget::currentChanged, this, [this](int index) {
        int tab = 0;
        if (index == 1) tab = 1;
        if (index == 2) tab = 2;
        m_toolbar->setActiveIndex(tab);
    });

    // Overlay for choosing sample/synth
    m_assignOverlay = new PadAssignOverlay(m_sampleSession, m_padBank, central);
    m_assignOverlay->hide();

    // Overlay for piano roll editor
    m_pianoRoll = new PianoRollOverlay(m_padBank, central);
    m_pianoRoll->hide();

    // Hold menu overlay
    m_holdMenu = new PadHoldMenuOverlay(m_padBank, central);
    m_holdMenu->hide();
    connect(m_seqPage, &SeqPageWidget::padOpenRequested, this, [this, editPage](int pad) {
        if (m_padBank && m_padBank->isSynth(pad)) {
            m_stack->setCurrentIndex(4);
            if (m_synthPage) {
                m_synthPage->setActivePad(pad);
            }
        } else {
            m_stack->setCurrentIndex(3);
        }
    });
    connect(m_seqPage, &SeqPageWidget::padAssignRequested, this, [this](int pad) {
        if (m_assignOverlay) {
            m_assignOverlay->showForPad(pad);
        }
    });
    connect(m_seqPage, &SeqPageWidget::padMenuRequested, this, [this](int pad) {
        if (m_holdMenu) {
            m_holdMenu->showForPad(pad);
        }
    });
    connect(m_assignOverlay, &PadAssignOverlay::closed, this, [this]() {
        m_stack->setCurrentIndex(0);
    });
    connect(m_pianoRoll, &PianoRollOverlay::closed, this, [this]() {
        m_stack->setCurrentIndex(0);
    });
    connect(m_pianoRoll, &PianoRollOverlay::stepsChanged, this,
            [this](int pad, const QVector<int> &steps) {
                if (m_seqPage) {
                    m_seqPage->applyPianoSteps(pad, steps);
                }
            });
    connect(m_pianoRoll, &PianoRollOverlay::notesChanged, this,
            [this](int pad, const QVector<int> &notes) {
                if (m_seqPage) {
                    m_seqPage->applyPianoNotes(pad, notes);
                }
            });
    connect(m_holdMenu, &PadHoldMenuOverlay::closed, this, [this]() {
        m_stack->setCurrentIndex(0);
    });
    connect(m_holdMenu, &PadHoldMenuOverlay::pianoRollRequested, this, [this](int pad) {
        if (m_pianoRoll) {
            m_pianoRoll->showForPad(pad);
        }
    });
    connect(m_holdMenu, &PadHoldMenuOverlay::replaceRequested, this, [this](int pad) {
        if (m_assignOverlay) {
            m_assignOverlay->showForPad(pad);
        }
    });

    m_projectMenu = new ProjectMenuOverlay(m_padBank, m_seqPage, m_fxPage, central);
    m_projectMenu->hide();

    auto *menuShortcut = new QShortcut(QKeySequence(Qt::Key_M), this);
    menuShortcut->setContext(Qt::ApplicationShortcut);
    connect(menuShortcut, &QShortcut::activated, this, [this]() {
        if (!m_projectMenu) {
            return;
        }
        if (m_projectMenu->isVisible()) {
            m_projectMenu->hide();
        } else {
            m_projectMenu->showMenu();
        }
    });

}

void MainWindow::closeEvent(QCloseEvent *event) {
    FramebufferCleaner::clearIfNeeded();
    QMainWindow::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_M) {
        if (m_projectMenu) {
            if (m_projectMenu->isVisible()) {
                m_projectMenu->hide();
            } else {
                m_projectMenu->showMenu();
            }
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}
