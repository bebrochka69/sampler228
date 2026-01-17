#include "SamplePageWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "SampleSession.h"
#include "Theme.h"
#include "WaveformRenderer.h"

SamplePageWidget::SamplePageWidget(SampleSession *session, QWidget *parent)
    : QWidget(parent), m_session(session) {
    setAutoFillBackground(false);

    refreshBrowser();
    rebuildProjects();

    if (m_session) {
        connect(m_session, &SampleSession::waveformChanged, this, [this]() { update(); });
        connect(m_session, &SampleSession::infoChanged, this, [this]() { update(); });
        connect(m_session, &SampleSession::playbackChanged, this, [this]() { update(); });
        connect(m_session, &SampleSession::errorChanged, this, [this]() { update(); });
    }
}

void SamplePageWidget::refreshBrowser() {
    m_browser.refresh();
    m_entries = m_browser.entries();
    m_scrollOffset = 0;
    update();
}

void SamplePageWidget::rebuildProjects() {
    m_projects.clear();
    const QString root = QDir::home().filePath("projects");
    QDir dir(root);
    if (!dir.exists()) {
        m_projects << "No projects";
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        m_projects << info.fileName();
    }
    if (m_projects.isEmpty()) {
        m_projects << "No projects";
    }
}

void SamplePageWidget::clampScroll() {
    const int rowHeight = 26;
    const int totalHeight = m_entries.size() * rowHeight;
    const int viewHeight = height() - 64;
    const int maxScroll = qMax(0, totalHeight - viewHeight);
    m_scrollOffset = qBound(0, m_scrollOffset, maxScroll);
}

void SamplePageWidget::wheelEvent(QWheelEvent *event) {
    if (m_entries.isEmpty()) {
        return;
    }
    const int delta = event->angleDelta().y();
    m_scrollOffset = m_scrollOffset - delta / 2;
    clampScroll();
    update();
}

void SamplePageWidget::mousePressEvent(QMouseEvent *event) {
    const QPointF pos = event->position();

    if (m_rescanRect.contains(pos)) {
        refreshBrowser();
        rebuildProjects();
        return;
    }

    if (m_playRect.contains(pos) && m_session) {
        m_session->play();
        return;
    }

    if (m_stopRect.contains(pos) && m_session) {
        m_session->stop();
        return;
    }

    const int leftWidth = static_cast<int>(width() * 0.34f);
    const QRectF listRect(12, 52, leftWidth - 24, height() - 64);
    if (!listRect.contains(pos)) {
        return;
    }

    const int rowHeight = 26;
    const int index = static_cast<int>((m_scrollOffset + (pos.y() - listRect.top())) / rowHeight);
    if (index < 0 || index >= m_entries.size()) {
        return;
    }

    SampleBrowserModel::Node *node = m_entries[index].node;
    if (!node) {
        return;
    }

    if (node->isDir) {
        m_browser.toggleExpanded(node);
    } else if (m_session) {
        m_session->setSource(node->path);
    }

    m_browser.setSelected(node);
    m_entries = m_browser.entries();
    clampScroll();
    update();
}

void SamplePageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    clampScroll();

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
    p.drawText(QRectF(16, 12, leftRect.width() - 56, 24), Qt::AlignLeft | Qt::AlignVCenter,
               "USB DRUM KITS");

    m_rescanRect = QRectF(leftRect.right() - 36, 14, 20, 20);
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(m_rescanRect, 4, 4);
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    p.drawText(m_rescanRect, Qt::AlignCenter, "R");

    const QRectF listRect(12, 52, leftRect.width() - 24, height() - 64);
    p.setClipRect(listRect);

    const int rowHeight = 26;
    int y = static_cast<int>(listRect.top()) - (m_scrollOffset % rowHeight);
    const int startIndex = qMax(0, m_scrollOffset / rowHeight);

    if (m_entries.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(10));
        p.drawText(listRect, Qt::AlignCenter, "USB not detected");
    } else {
        for (int i = startIndex; i < m_entries.size(); ++i) {
            if (y > listRect.bottom()) {
                break;
            }

            const SampleBrowserModel::Entry &entry = m_entries[i];
            const QRectF row(listRect.left(), y, listRect.width(), rowHeight - 2);

            if (entry.node == m_browser.selected()) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::withAlpha(Theme::accent(), 60));
                p.drawRoundedRect(row, 4, 4);
            }

            const float x = row.left() + entry.depth * 16.0f;
            if (entry.node && entry.node->isDir) {
                p.setPen(Theme::textMuted());
                QPolygonF tri;
                if (entry.node->expanded) {
                    tri << QPointF(x, row.center().y() - 3)
                        << QPointF(x + 6, row.center().y() - 3)
                        << QPointF(x + 3, row.center().y() + 3);
                } else {
                    tri << QPointF(x, row.center().y() - 4)
                        << QPointF(x + 6, row.center().y())
                        << QPointF(x, row.center().y() + 4);
                }
                p.setBrush(Theme::textMuted());
                p.drawPolygon(tri);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::accentAlt());
                p.drawEllipse(QPointF(x + 3, row.center().y()), 2.5, 2.5);
            }

            p.setPen(entry.node == m_browser.selected() ? Theme::text() : Theme::textMuted());
            const QString label = entry.node ? entry.node->name : QString();
            p.drawText(QRectF(x + 12, row.top(), row.width() - 12, row.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, label);

            y += rowHeight;
        }
    }

    p.setClipping(false);

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

    QVector<float> wave;
    if (m_session && m_session->hasWaveform()) {
        wave = m_session->waveform();
    }
    WaveformRenderer::drawWaveform(p, waveRect, wave, Theme::accentAlt(),
                                   Theme::withAlpha(Theme::textMuted(), 120));

    p.setFont(Theme::baseFont(10, QFont::DemiBold));
    p.setPen(Theme::textMuted());
    const QString info = m_session ? m_session->infoText() : QString("No session");
    p.drawText(QRectF(waveContainer.left() + 12, waveRect.bottom() + 10, waveContainer.width() - 24, 20),
               Qt::AlignLeft | Qt::AlignVCenter, info);

    if (m_session && !m_session->errorText().isEmpty()) {
        p.setFont(Theme::baseFont(9));
        p.setPen(Theme::danger());
        p.drawText(QRectF(waveContainer.left() + 12, waveRect.bottom() + 26, waveContainer.width() - 24, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, m_session->errorText());
    }

    // Preview controls.
    const QRectF controlsRect(waveContainer.right() - 120, waveContainer.bottom() - 32, 108, 24);
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(controlsRect, 6, 6);

    m_playRect = QRectF(controlsRect.left() + 12, controlsRect.center().y() - 7, 14, 14);
    m_stopRect = QRectF(controlsRect.left() + 44, controlsRect.center().y() - 6, 12, 12);

    const QPointF playCenter(m_playRect.center());
    QPolygonF playTri;
    playTri << QPointF(playCenter.x() - 4, playCenter.y() - 6)
            << QPointF(playCenter.x() + 6, playCenter.y())
            << QPointF(playCenter.x() - 4, playCenter.y() + 6);
    p.setBrush(m_session && m_session->isPlaying() ? Theme::accentAlt() : Theme::accent());
    p.setPen(Qt::NoPen);
    p.drawPolygon(playTri);

    p.setBrush(Theme::accentAlt());
    p.drawRect(m_stopRect);

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(9));
    p.drawText(QRectF(controlsRect.left() + 70, controlsRect.top(), 36, controlsRect.height()),
               Qt::AlignCenter, "A/B");
}
