#include "MainWindow.h"

#include <QStackedWidget>
#include <QVBoxLayout>

#include "ui/BpmArcWidget.h"
#include "ui/EditPageWidget.h"
#include "ui/SamplePageWidget.h"
#include "ui/SeqPageWidget.h"
#include "ui/SimplePageWidget.h"
#include "ui/TopToolbarWidget.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("GrooveBox UI");
    setFixedSize(1280, 720);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_toolbar = new TopToolbarWidget(central);

    m_stack = new QStackedWidget(central);
    m_stack->addWidget(new SamplePageWidget(m_stack));
    m_stack->addWidget(new EditPageWidget(m_stack));
    m_stack->addWidget(new SeqPageWidget(m_stack));
    m_stack->addWidget(new SimplePageWidget("FX", m_stack));
    m_stack->addWidget(new SimplePageWidget("ARRANG", m_stack));

    layout->addWidget(m_toolbar);
    layout->addWidget(m_stack, 1);

    setCentralWidget(central);

    connect(m_toolbar, &TopToolbarWidget::pageSelected, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_stack, &QStackedWidget::currentChanged, m_toolbar, &TopToolbarWidget::setActiveIndex);

    m_bpmArc = new BpmArcWidget(this);
    m_bpmArc->setBpm(124);
    const int arcSize = m_toolbar->height() * 2;
    m_bpmArc->setFixedSize(arcSize, arcSize);
    m_bpmArc->move(width() - arcSize, 0);
    m_bpmArc->raise();
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    if (!m_toolbar || !m_bpmArc) {
        return;
    }

    const int arcSize = m_toolbar->height() * 2;
    m_bpmArc->setFixedSize(arcSize, arcSize);
    m_bpmArc->move(width() - arcSize, 0);
    m_bpmArc->raise();
}
