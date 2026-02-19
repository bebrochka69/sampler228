#include "PadAssignOverlay.h"

#include <QMouseEvent>
#include <QPainter>
#include <QStackedWidget>

#include "PadBank.h"
#include "SamplePageWidget.h"
#include "SynthPageWidget.h"
#include "Theme.h"

class SynthSelectWidget : public QWidget {
    Q_OBJECT
public:
    explicit SynthSelectWidget(PadBank *pads, QWidget *parent = nullptr)
        : QWidget(parent), m_pads(pads) {
        setAutoFillBackground(false);
        setFocusPolicy(Qt::StrongFocus);
        if (m_pads) {
            m_items = PadBank::synthTypes();
        }
        if (m_items.isEmpty()) {
            m_items << "DX7";
        }
    }

    void setActivePad(int pad) { m_activePad = pad; }

signals:
    void synthAssigned();

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        Theme::paintBackground(p, rect());
        Theme::applyRenderHints(p);

        const QRectF panel = rect().adjusted(Theme::px(16), Theme::px(16),
                                             -Theme::px(16), -Theme::px(16));
        p.setBrush(Theme::bg1());
        p.setPen(QPen(Theme::stroke(), 1.2));
        p.drawRoundedRect(panel, Theme::px(12), Theme::px(12));

        p.setFont(Theme::condensedFont(14, QFont::Bold));
        p.setPen(Theme::accent());
        p.drawText(QRectF(panel.left() + Theme::px(12), panel.top() + Theme::px(8),
                          panel.width() - Theme::px(24), Theme::px(24)),
                   Qt::AlignLeft | Qt::AlignVCenter, "SYNTH SELECT");

        const float rowH = Theme::pxF(46.0f);
        const QRectF listRect(panel.left() + Theme::px(12),
                              panel.top() + Theme::px(40),
                              panel.width() - Theme::px(24),
                              panel.height() - Theme::px(56));
        m_rows.clear();

        for (int i = 0; i < m_items.size(); ++i) {
            QRectF row(listRect.left(), listRect.top() + i * rowH,
                       listRect.width(), rowH - Theme::px(6));
            m_rows.push_back(row);
            p.setBrush(Theme::bg2());
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.drawRoundedRect(row, Theme::px(8), Theme::px(8));
            p.setPen(Theme::text());
            p.setFont(Theme::baseFont(12, QFont::DemiBold));
            p.drawText(row, Qt::AlignCenter, m_items[i]);
        }
    }

    void mousePressEvent(QMouseEvent *event) override {
        const QPointF pos = event->position();
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].contains(pos) && m_pads) {
                const QString type = (i >= 0 && i < m_items.size()) ? m_items[i] : QString("DX7");
                QString preset;
                const QString upper = type.trimmed().toUpper();
                if (upper == "SIMPLE" || upper == "FM" || upper == "SERUM" || upper == "VITALYA" || upper == "VITAL") {
                    const QStringList presets = PadBank::synthPresetsForBank("SIMPLE");
                    preset = presets.isEmpty() ? QString("INIT") : presets.first();
                } else {
                    const QStringList presets = PadBank::synthPresets();
                    preset = presets.isEmpty() ? QString("PROGRAM 01") : presets.first();
                }
                m_pads->setSynth(m_activePad, QString("%1:%2").arg(type).arg(preset));
                emit synthAssigned();
                return;
            }
        }
    }

private:
    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    QStringList m_items;
    QVector<QRectF> m_rows;
};

PadAssignOverlay::PadAssignOverlay(SampleSession *session, PadBank *pads, QWidget *parent)
    : QWidget(parent), m_pads(pads) {
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, true);
    setVisible(false);

    m_stack = new QStackedWidget(this);
    m_samplePage = new SamplePageWidget(session, pads, m_stack);
    m_samplePage->setAssignMode(true);
    m_synthSelect = new SynthSelectWidget(pads, m_stack);

    m_stack->addWidget(m_samplePage);
    m_stack->addWidget(m_synthSelect);

    connect(m_samplePage, &SamplePageWidget::sampleAssigned, this, &PadAssignOverlay::handleAssigned);
    if (auto *synth = qobject_cast<SynthSelectWidget *>(m_synthSelect)) {
        connect(synth, &SynthSelectWidget::synthAssigned, this, &PadAssignOverlay::handleAssigned);
    }
}

void PadAssignOverlay::showForPad(int pad) {
    showForPad(pad, 0);
}

void PadAssignOverlay::showForPad(int pad, int tabIndex) {
    m_activePad = pad;
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    if (m_pads) {
        m_pads->setActivePad(pad);
    }
    if (auto *synth = qobject_cast<SynthSelectWidget *>(m_synthSelect)) {
        synth->setActivePad(pad);
    }
    switchTab(tabIndex);
    setVisible(true);
    raise();
    update();
}

void PadAssignOverlay::handleAssigned() {
    setVisible(false);
    emit closed();
}

void PadAssignOverlay::switchTab(int tab) {
    m_tab = tab;
    if (m_stack) {
        m_stack->setCurrentIndex(tab);
    }
}

void PadAssignOverlay::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (m_closeRect.contains(pos)) {
        setVisible(false);
        emit closed();
        return;
    }
    if (m_samplesTab.contains(pos)) {
        switchTab(0);
        update();
        return;
    }
    if (m_synthTab.contains(pos)) {
        switchTab(1);
        update();
        return;
    }
}

void PadAssignOverlay::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.setBrush(Theme::withAlpha(Theme::bg0(), 230));
    p.setPen(Qt::NoPen);
    p.drawRect(rect());

    const QRectF panel = rect().adjusted(Theme::px(18), Theme::px(18),
                                         -Theme::px(18), -Theme::px(18));
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawRoundedRect(panel, Theme::px(12), Theme::px(12));

    const float tabH = Theme::pxF(36.0f);
    m_samplesTab = QRectF(panel.left() + Theme::px(12), panel.top() + Theme::px(8),
                          Theme::px(120), tabH);
    m_synthTab = QRectF(m_samplesTab.right() + Theme::px(8), panel.top() + Theme::px(8),
                        Theme::px(120), tabH);
    m_closeRect = QRectF(panel.right() - Theme::px(28), panel.top() + Theme::px(10),
                         Theme::px(18), Theme::px(18));

    auto drawTab = [&](const QRectF &r, const QString &label, bool active) {
        p.setBrush(active ? Theme::accentAlt() : Theme::bg2());
        p.setPen(QPen(Theme::stroke(), 1.0));
        p.drawRoundedRect(r, Theme::px(8), Theme::px(8));
        p.setPen(active ? Theme::bg0() : Theme::text());
        p.setFont(Theme::condensedFont(11, QFont::Bold));
        p.drawText(r, Qt::AlignCenter, label);
    };
    drawTab(m_samplesTab, "SAMPLES", m_tab == 0);
    drawTab(m_synthTab, "SYNTH", m_tab == 1);

    p.setPen(QPen(Theme::text(), 1.6));
    p.drawLine(QPointF(m_closeRect.left(), m_closeRect.top()),
               QPointF(m_closeRect.right(), m_closeRect.bottom()));
    p.drawLine(QPointF(m_closeRect.right(), m_closeRect.top()),
               QPointF(m_closeRect.left(), m_closeRect.bottom()));

    const QRectF stackRect(panel.left() + Theme::px(8), panel.top() + Theme::px(48),
                           panel.width() - Theme::px(16), panel.height() - Theme::px(56));
    if (m_stack) {
        m_stack->setGeometry(stackRect.toRect());
    }
}

#include "PadAssignOverlay.moc"
