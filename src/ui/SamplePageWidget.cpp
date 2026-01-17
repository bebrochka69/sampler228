#include "SamplePageWidget.h"

#include <QPainter>

#include "Theme.h"
#include "WaveformRenderer.h"

SamplePageWidget::SamplePageWidget(QWidget *parent) : QWidget(parent) {
    setAutoFillBackground(false);

    m_navItems = {
        {"USB:/KITS", 0, true, true},
        {"808", 1, true, false},
        {"BD_808.wav", 2, false, false},
        {"SD_808.wav", 2, false, false},
        {"909", 1, true, false},
        {"BD_909.wav", 2, false, false},
        {"Tape", 1, true, false},
        {"TapeKick.wav", 2, false, false},
        {"Hybrid", 1, true, false},
        {"Hat_01.wav", 2, false, false},
    };

    m_projects << "Project_A" << "NightDrive" << "Warehouse" << "ThinLines";
    m_wave = WaveformRenderer::makeDemoWave(180, 11);
}

void SamplePageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, Theme::bg0());
    bg.setColorAt(1.0, Theme::bg2());
    p.fillRect(rect(), bg);

    const int leftWidth = static_cast<int>(width() * 0.34f);
    const QRectF leftRect(0, 0, leftWidth, height());
    const QRectF rightRect(leftWidth, 0, width() - leftWidth, height());

    p.fillRect(leftRect, Theme::bg1());
    p.fillRect(rightRect, Theme::bg0());

    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawLine(QPointF(leftRect.right(), 0), QPointF(leftRect.right(), height()));

    p.setFont(Theme::condensedFont(14, QFont::DemiBold));
    p.setPen(Theme::text());
    p.drawText(QRectF(16, 12, leftRect.width() - 32, 24), Qt::AlignLeft | Qt::AlignVCenter,
               "USB DRUM KITS");

    const int rowHeight = 28;
    int y = 52;
    p.setFont(Theme::baseFont(10));

    for (const auto &item : m_navItems) {
        const QRectF row(12, y, leftRect.width() - 24, rowHeight - 2);
        if (item.selected) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::withAlpha(Theme::accent(), 60));
            p.drawRoundedRect(row, 4, 4);
        }

        const float x = row.left() + item.indent * 16.0f;
        if (item.isFolder) {
            p.setPen(Theme::textMuted());
            QPolygonF tri;
            tri << QPointF(x, row.center().y() - 4)
                << QPointF(x + 6, row.center().y())
                << QPointF(x, row.center().y() + 4);
            p.setBrush(Theme::textMuted());
            p.drawPolygon(tri);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::accentAlt());
            p.drawEllipse(QPointF(x + 3, row.center().y()), 2.5, 2.5);
        }

        p.setPen(item.selected ? Theme::text() : Theme::textMuted());
        p.drawText(QRectF(x + 12, row.top(), row.width() - 12, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, item.label);

        y += rowHeight;
    }

    const QRectF projectsRect(rightRect.left() + 16, 16, rightRect.width() - 32, height() * 0.28f);
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg1());
    p.drawRoundedRect(projectsRect, 8, 8);

    p.setPen(Theme::text());
    p.setFont(Theme::condensedFont(13, QFont::DemiBold));
    p.drawText(projectsRect.adjusted(12, 8, -12, -8), Qt::AlignLeft | Qt::AlignTop, "PROJECTS");

    p.setFont(Theme::baseFont(10));
    int py = static_cast<int>(projectsRect.top() + 34);
    for (int i = 0; i < m_projects.size(); ++i) {
        QRectF row(projectsRect.left() + 12, py, projectsRect.width() - 24, 22);
        if (i == 1) {
            p.setBrush(Theme::withAlpha(Theme::accentAlt(), 60));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(row, 4, 4);
        }
        p.setPen(Theme::textMuted());
        p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, m_projects[i]);
        py += 24;
    }

    const QRectF waveContainer(rightRect.left() + 16, projectsRect.bottom() + 16,
                               rightRect.width() - 32, height() - projectsRect.bottom() - 32);
    p.setBrush(Theme::bg1());
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(waveContainer, 8, 8);

    const QRectF waveRect = waveContainer.adjusted(12, 12, -12, -52);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.drawRoundedRect(waveRect, 6, 6);

    WaveformRenderer::drawWaveform(p, waveRect, m_wave, Theme::accentAlt(),
                                   Theme::withAlpha(Theme::textMuted(), 120));

    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    p.setPen(Theme::textMuted());
    const QString info = "Len 1.24s  |  44.1 kHz  |  16-bit";
    p.drawText(QRectF(waveContainer.left() + 12, waveRect.bottom() + 10, waveContainer.width() - 24, 20),
               Qt::AlignLeft | Qt::AlignVCenter, info);

    // Preview controls.
    const QRectF controlsRect(waveContainer.right() - 120, waveContainer.bottom() - 32, 108, 24);
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(controlsRect, 6, 6);

    const QPointF playCenter(controlsRect.left() + 24, controlsRect.center().y());
    QPolygonF playTri;
    playTri << QPointF(playCenter.x() - 4, playCenter.y() - 6)
            << QPointF(playCenter.x() + 6, playCenter.y())
            << QPointF(playCenter.x() - 4, playCenter.y() + 6);
    p.setBrush(Theme::accent());
    p.setPen(Qt::NoPen);
    p.drawPolygon(playTri);

    const QRectF stopRect(controlsRect.left() + 48, controlsRect.center().y() - 6, 12, 12);
    p.setBrush(Theme::accentAlt());
    p.drawRect(stopRect);

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9));
    p.drawText(QRectF(controlsRect.left() + 70, controlsRect.top(), 36, controlsRect.height()),
               Qt::AlignCenter, "A/B");
}
