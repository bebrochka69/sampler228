#include "MainWindow.h"

#include <QCloseEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "FramebufferCleaner.h"
#include "ui/EditPageWidget.h"
#include "ui/FxPageWidget.h"
#include "ui/PadAssignOverlay.h"
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
    m_sampleSession = new SampleSession(this);
    auto *seqPage = new SeqPageWidget(m_padBank, m_stack);
    auto *editPage = new EditPageWidget(m_sampleSession, m_padBank, m_stack);
    auto *fxPage = new FxPageWidget(m_padBank, m_stack);
    m_synthPage = new SynthPageWidget(m_padBank, m_stack);

    m_stack->addWidget(seqPage);                      // 0
    m_stack->addWidget(fxPage);                       // 1
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
    connect(seqPage, &SeqPageWidget::padOpenRequested, this, [this, editPage](int pad) {
        if (m_padBank && m_padBank->isSynth(pad)) {
            m_stack->setCurrentIndex(4);
            if (m_synthPage) {
                m_synthPage->setActivePad(pad);
            }
        } else {
            m_stack->setCurrentIndex(3);
        }
    });
    connect(seqPage, &SeqPageWidget::padAssignRequested, this, [this](int pad) {
        if (m_assignOverlay) {
            m_assignOverlay->showForPad(pad);
        }
    });
    connect(m_assignOverlay, &PadAssignOverlay::closed, this, [this]() {
        m_stack->setCurrentIndex(0);
    });

}

void MainWindow::closeEvent(QCloseEvent *event) {
    FramebufferCleaner::clearIfNeeded();
    QMainWindow::closeEvent(event);
}
