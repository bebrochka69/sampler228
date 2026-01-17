#include "MainWindow.h"

#include <QStackedWidget>
#include <QVBoxLayout>

#include "ui/EditPageWidget.h"
#include "ui/SamplePageWidget.h"
#include "ui/SeqPageWidget.h"
#include "ui/SimplePageWidget.h"
#include "ui/TopToolbarWidget.h"
#include "PadBank.h"
#include "SampleSession.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("GrooveBox UI");
    setFixedSize(1280, 720);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_padBank = new PadBank(this);
    m_toolbar = new TopToolbarWidget(m_padBank, central);

    m_stack = new QStackedWidget(central);
    m_sampleSession = new SampleSession(this);
    m_stack->addWidget(new SamplePageWidget(m_sampleSession, m_padBank, m_stack));
    m_stack->addWidget(new EditPageWidget(m_sampleSession, m_stack));
    m_stack->addWidget(new SeqPageWidget(m_stack));
    m_stack->addWidget(new SimplePageWidget("FX", m_stack));
    m_stack->addWidget(new SimplePageWidget("ARRANGE", m_stack));

    layout->addWidget(m_toolbar);
    layout->addWidget(m_stack, 1);

    setCentralWidget(central);

    connect(m_toolbar, &TopToolbarWidget::pageSelected, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_stack, &QStackedWidget::currentChanged, m_toolbar, &TopToolbarWidget::setActiveIndex);

}
