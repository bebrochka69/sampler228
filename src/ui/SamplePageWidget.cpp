#include "SamplePageWidget.h"

#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "PadBank.h"
#include "SampleSession.h"
#include "Theme.h"

SamplePageWidget::SamplePageWidget(SampleSession *session, PadBank *pads, QWidget *parent)
    : QWidget(parent), m_session(session), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    if (!Theme::liteMode()) {
        m_ambientTimer.setInterval(33);
        connect(&m_ambientTimer, &QTimer::timeout, this, [this]() {
            if (isVisible()) {
                update();
            }
        });
        m_ambientTimer.start();
    }

    refreshBrowser();
    rebuildProjects();

    if (m_session) {
        connect(m_session, &SampleSession::waveformChanged, this, [this]() { update(); });
        connect(m_session, &SampleSession::infoChanged, this, [this]() { update(); });
        connect(m_session, &SampleSession::playbackChanged, this, [this]() { update(); });
        connect(m_session, &SampleSession::errorChanged, this, [this]() { update(); });
    }

    if (m_pads) {
        connect(m_pads, &PadBank::padChanged, this, [this](int) { update(); });
        connect(m_pads, &PadBank::activePadChanged, this, [this](int) { update(); });
    }
}

void SamplePageWidget::refreshBrowser() {
    m_browser.refresh();
    m_entries = m_browser.entries();

    if (!m_entries.isEmpty()) {
        selectIndex(0);
    } else {
        m_selectedIndex = -1;
    }

    m_scrollOffset = 0;
    update();
}

void SamplePageWidget::rebuildProjects() {
    m_projects.clear();
    const QString root = QDir::home().filePath("projects");
    QDir dir(root);
    if (!dir.exists()) {
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : entries) {
        m_projects << info.fileName();
    }
}

void SamplePageWidget::clampScroll() {
    const int rowHeight = Theme::px(26);
    const int headerHeight = Theme::px(28);
    const int contentTop = headerHeight + Theme::px(8);
    const int leftWidth = static_cast<int>(width() * 0.62f);
    const QRectF leftRect(Theme::px(12), contentTop, leftWidth - Theme::px(18),
                          height() - contentTop - Theme::px(12));
    const QRectF listRect(leftRect.left(), leftRect.top() + Theme::px(36),
                          leftRect.width(), leftRect.height() - Theme::px(36));
    const int totalHeight = m_entries.size() * rowHeight;
    const int viewHeight = static_cast<int>(listRect.height());
    const int maxScroll = qMax(0, totalHeight - viewHeight);
    m_scrollOffset = qBound(0, m_scrollOffset, maxScroll);
}

void SamplePageWidget::selectIndex(int index) {
    if (m_entries.isEmpty()) {
        m_selectedIndex = -1;
        return;
    }

    const int clamped = qBound(0, index, m_entries.size() - 1);
    m_selectedIndex = clamped;
    SampleBrowserModel::Node *node = m_entries[clamped].node;
    m_browser.setSelected(node);

    if (node && !node->isDir && m_session) {
        m_session->setSource(node->path, SampleSession::DecodeMode::None);
    }

    const int rowHeight = Theme::px(26);
    const int headerHeight = Theme::px(28);
    const int contentTop = headerHeight + Theme::px(8);
    const int leftWidth = static_cast<int>(width() * 0.62f);
    const QRectF leftRect(Theme::px(12), contentTop, leftWidth - Theme::px(18),
                          height() - contentTop - Theme::px(12));
    const QRectF listRect(leftRect.left(), leftRect.top() + Theme::px(36),
                          leftRect.width(), leftRect.height() - Theme::px(36));
    const int viewHeight = static_cast<int>(listRect.height());
    const int posY = clamped * rowHeight;
    if (posY < m_scrollOffset) {
        m_scrollOffset = posY;
    } else if (posY > m_scrollOffset + viewHeight - rowHeight) {
        m_scrollOffset = posY - viewHeight + rowHeight;
    }
    clampScroll();
}

int SamplePageWidget::indexOfNode(SampleBrowserModel::Node *node) const {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].node == node) {
            return i;
        }
    }
    return -1;
}

QString SamplePageWidget::currentDirLabel() const {
    SampleBrowserModel::Node *node = m_browser.selected();
    if (!node) {
        return "DIR: (no media)";
    }
    if (node->isDir) {
        return QString("DIR: %1").arg(node->path);
    }
    if (node->parent) {
        return QString("DIR: %1").arg(node->parent->path);
    }
    return QString("DIR: %1").arg(node->path);
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

void SamplePageWidget::keyPressEvent(QKeyEvent *event) {
    if (m_entries.isEmpty()) {
        return;
    }

    const int key = event->key();
    if (key >= Qt::Key_1 && key <= Qt::Key_8) {
        if (m_pads) {
            m_pads->setActivePad(key - Qt::Key_1);
        }
        update();
        return;
    }

    SampleBrowserModel::Node *node = m_browser.selected();

    switch (key) {
        case Qt::Key_Down:
            selectIndex(m_selectedIndex + 1);
            update();
            break;
        case Qt::Key_Up:
            selectIndex(m_selectedIndex - 1);
            update();
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (node && node->isDir) {
                m_browser.toggleExpanded(node);
                m_entries = m_browser.entries();
                m_selectedIndex = indexOfNode(node);
                clampScroll();
                update();
            } else if (node && !node->isDir && m_pads && m_assignMode) {
                m_pads->setPadPath(m_pads->activePad(), node->path);
                emit sampleAssigned();
            }
            break;
        case Qt::Key_Backspace:
            if (node && node->parent) {
                m_browser.setSelected(node->parent);
                m_entries = m_browser.entries();
                m_selectedIndex = indexOfNode(node->parent);
                clampScroll();
                update();
            }
            break;
        case Qt::Key_L:
            if (node && !node->isDir && m_pads) {
                m_pads->setPadPath(m_pads->activePad(), node->path);
                update();
            }
            break;
        case Qt::Key_F:
        case Qt::Key_R:
            refreshBrowser();
            rebuildProjects();
            break;
        case Qt::Key_Space:
            if (m_session) {
                if (m_session->isPlaying()) {
                    m_session->stop();
                } else {
                    m_session->play();
                }
            }
            break;
        default:
            break;
    }
}

void SamplePageWidget::mousePressEvent(QMouseEvent *event) {
    setFocus(Qt::MouseFocusReason);
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

    const int headerHeight = Theme::px(28);
    const int contentTop = headerHeight + Theme::px(8);
    const int leftWidth = static_cast<int>(width() * 0.62f);
    const QRectF leftRect(Theme::px(12), contentTop, leftWidth - Theme::px(18),
                          height() - contentTop - Theme::px(12));
    const QRectF listRect(leftRect.left(), leftRect.top() + Theme::px(36),
                          leftRect.width(), leftRect.height() - Theme::px(36));
    if (!listRect.contains(pos)) {
        return;
    }

    const int rowHeight = Theme::px(26);
    const int index = static_cast<int>((m_scrollOffset + (pos.y() - listRect.top())) / rowHeight);
    if (index < 0 || index >= m_entries.size()) {
        return;
    }

    SampleBrowserModel::Node *node = m_entries[index].node;
    if (!node) {
        return;
    }

    m_browser.setSelected(node);
    m_selectedIndex = index;

    if (node->isDir) {
        m_browser.toggleExpanded(node);
    } else if (m_session) {
        m_session->setSource(node->path, SampleSession::DecodeMode::None);
        if (m_pads && m_assignMode) {
            m_pads->setPadPath(m_pads->activePad(), node->path);
            emit sampleAssigned();
            return;
        }
    }

    m_entries = m_browser.entries();
    m_selectedIndex = indexOfNode(node);
    clampScroll();
    update();
}

void SamplePageWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    clampScroll();

    QPainter p(this);
    Theme::paintBackground(p, rect());
    Theme::applyRenderHints(p);
    const bool lite = Theme::liteMode();

    const int headerHeight = Theme::px(28);
    const QRectF headerRect(0, 0, width(), headerHeight);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::bg3());
    p.drawRoundedRect(headerRect.adjusted(Theme::px(4), Theme::px(2),
                                          -Theme::px(4), -Theme::px(2)),
                      Theme::px(10), Theme::px(10));
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.drawLine(QPointF(0, headerRect.bottom()), QPointF(width(), headerRect.bottom()));

    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.setPen(Theme::accent());
    p.drawText(QRectF(12, 0, width() * 0.5, headerHeight),
               Qt::AlignLeft | Qt::AlignVCenter, "SAMPLES");
    p.setPen(Theme::accentAlt());
    p.drawText(QRectF(width() * 0.5, 0, width() * 0.5 - 12, headerHeight),
               Qt::AlignRight | Qt::AlignVCenter, "USB BROWSER");

    const int contentTop = headerHeight + Theme::px(8);
    const int leftWidth = static_cast<int>(width() * 0.62f);
    const QRectF leftRect(Theme::px(12), contentTop, leftWidth - Theme::px(18),
                          height() - contentTop - Theme::px(12));
    const QRectF rightRect(leftRect.right() + Theme::px(10), contentTop,
                           width() - leftRect.right() - Theme::px(22),
                           height() - contentTop - Theme::px(12));

    // Left panel.
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.setBrush(Theme::bg1());
    p.drawRoundedRect(leftRect, Theme::px(12), Theme::px(12));

    const QRectF dirRect(leftRect.left() + Theme::px(8), leftRect.top() + Theme::px(6),
                         leftRect.width() - Theme::px(16), Theme::px(26));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(dirRect, Theme::px(8), Theme::px(8));

    QFont dirFont = Theme::baseFont(9, QFont::DemiBold);
    p.setFont(dirFont);
    p.setPen(Theme::text());
    QFontMetrics dirFm(dirFont);
    const QString dirText =
        dirFm.elidedText(currentDirLabel(), Qt::ElideRight, dirRect.width() - Theme::px(32));
    p.drawText(dirRect.adjusted(Theme::px(8), 0, -Theme::px(30), 0),
               Qt::AlignLeft | Qt::AlignVCenter, dirText);

    m_rescanRect = QRectF(dirRect.right() - Theme::px(22), dirRect.top() + Theme::px(4),
                          Theme::px(16), Theme::px(16));
    p.setPen(QPen(Theme::accent(), 1.0));
    p.setBrush(Theme::bg1());
    p.drawRoundedRect(m_rescanRect, Theme::px(4), Theme::px(4));
    p.setFont(Theme::baseFont(9, QFont::Bold));
    p.drawText(m_rescanRect, Qt::AlignCenter, "R");

    // Browser list.
    const QRectF listRect(leftRect.left() + Theme::px(8), dirRect.bottom() + Theme::px(8),
                          leftRect.width() - Theme::px(16),
                          leftRect.bottom() - dirRect.bottom() - Theme::px(14));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(listRect, Theme::px(10), Theme::px(10));

    // Memory layer behind list (VIDEO_04).
    if (!lite) {
        p.save();
        p.setClipRect(listRect.adjusted(Theme::px(2), Theme::px(2),
                                        -Theme::px(2), -Theme::px(2)));
        p.setCompositionMode(QPainter::CompositionMode_SoftLight);
        Theme::drawFog(p, listRect, QColor(220, 200, 220, 26), 0.10f, 0.05f, 0.9f);
        Theme::drawFog(p, listRect, QColor(170, 200, 220, 24), 0.08f, 0.06f, 0.8f);
        Theme::drawGrain(p, listRect, 0.06f);
        p.restore();
    }

    p.save();
    p.setClipRect(listRect.adjusted(Theme::px(2), Theme::px(2),
                                    -Theme::px(2), -Theme::px(2)));

    const int rowHeight = Theme::px(26);
    int y = static_cast<int>(listRect.top()) - (m_scrollOffset % rowHeight);
    const int startIndex = qMax(0, m_scrollOffset / rowHeight);

    QFont rowFont = Theme::baseFont(10);
    p.setFont(rowFont);

    if (m_entries.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.drawText(listRect, Qt::AlignCenter, "NO USB MEDIA");
    } else {
        for (int i = startIndex; i < m_entries.size(); ++i) {
            if (y > listRect.bottom()) {
                break;
            }

            const SampleBrowserModel::Entry &entry = m_entries[i];
            const QRectF row(listRect.left() + Theme::px(4), y,
                             listRect.width() - Theme::px(8), rowHeight - Theme::px(2));

            const bool selected = (i == m_selectedIndex);
            const QColor rowColor = (i % 2 == 0) ? Theme::bg2() : Theme::bg1();
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.setBrush(selected ? Theme::accentAlt() : rowColor);
            p.drawRoundedRect(row, Theme::px(6), Theme::px(6));

            if (selected) {
                p.setBrush(Theme::accent());
                p.setPen(Qt::NoPen);
                p.drawRect(QRectF(row.left() + Theme::px(2), row.top() + Theme::px(2),
                                  Theme::px(4), row.height() - Theme::px(4)));
            }

            const float indent = entry.depth * Theme::pxF(12.0f);
            QString label;
            if (entry.node && entry.node->isDir) {
                label = QString("[DIR] %1").arg(entry.node->name);
            } else if (entry.node) {
                label = entry.node->name;
            }

            p.setPen(selected ? Theme::bg0() : Theme::text());
            p.drawText(QRectF(row.left() + Theme::px(10) + indent, row.top(),
                              row.width() - Theme::px(12), row.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, label);

            y += rowHeight;
        }
    }

    p.restore();

    // Right panels.
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.setBrush(Theme::bg1());
    p.drawRoundedRect(rightRect, Theme::px(12), Theme::px(12));

    const QRectF projectsRect(rightRect.left() + Theme::px(8), rightRect.top() + Theme::px(6),
                              rightRect.width() - Theme::px(16), Theme::px(130));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(projectsRect, Theme::px(10), Theme::px(10));

    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(11, QFont::Bold));
    p.drawText(projectsRect.adjusted(Theme::px(8), Theme::px(4),
                                     -Theme::px(8), -Theme::px(4)),
               Qt::AlignLeft | Qt::AlignTop, "PROJECT BANK");

    p.setFont(Theme::baseFont(9));
    p.setPen(Theme::text());
    int py = static_cast<int>(projectsRect.top() + Theme::px(24));
    if (m_projects.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(projectsRect.left() + Theme::px(8), py,
                          projectsRect.width() - Theme::px(16), Theme::px(16)),
                   Qt::AlignLeft | Qt::AlignVCenter, "No projects");
    } else {
        for (int i = 0; i < m_projects.size(); ++i) {
            QRectF row(projectsRect.left() + Theme::px(8), py,
                       projectsRect.width() - Theme::px(16), Theme::px(16));
            p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, m_projects[i]);
            py += Theme::px(18);
            if (py > projectsRect.bottom() - Theme::px(10)) {
                break;
            }
        }
    }

    const QRectF previewRect(rightRect.left() + Theme::px(8),
                             projectsRect.bottom() + Theme::px(10),
                             rightRect.width() - Theme::px(16),
                             rightRect.bottom() - projectsRect.bottom() - Theme::px(16));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(previewRect, Theme::px(10), Theme::px(10));

    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(11, QFont::Bold));
    p.drawText(QRectF(previewRect.left() + Theme::px(8), previewRect.top() + Theme::px(4),
                      previewRect.width() - Theme::px(16), Theme::px(16)),
               Qt::AlignLeft | Qt::AlignVCenter, "PREVIEW");

    const QRectF infoRect(previewRect.left() + Theme::px(10),
                          previewRect.top() + Theme::px(26),
                          previewRect.width() - Theme::px(20),
                          previewRect.height() - Theme::px(36));
    const float infoSplit = 0.62f;
    const QRectF infoLeft(infoRect.left(), infoRect.top(), infoRect.width() * infoSplit - 6,
                          infoRect.height());
    const QRectF infoRight(infoRect.left() + infoRect.width() * infoSplit + 6, infoRect.top(),
                           infoRect.width() * (1.0f - infoSplit) - 6, infoRect.height());

    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg1());
    p.drawRoundedRect(infoRect, Theme::px(10), Theme::px(10));

    // Transport buttons.
    const QRectF transportRect(infoRight.left() + Theme::px(6), infoRight.top() + Theme::px(6),
                               infoRight.width() - Theme::px(12), Theme::px(20));
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRoundedRect(transportRect, Theme::px(6), Theme::px(6));

    m_playRect = QRectF(transportRect.left() + Theme::px(6),
                        transportRect.top() + Theme::px(4),
                        Theme::px(12), Theme::px(12));
    m_stopRect = QRectF(transportRect.left() + Theme::px(26),
                        transportRect.top() + Theme::px(4),
                        Theme::px(12), Theme::px(12));

    const QPointF playCenter(m_playRect.center());
    QPolygonF playTri;
    playTri << QPointF(playCenter.x() - Theme::px(4), playCenter.y() - Theme::px(5))
            << QPointF(playCenter.x() + Theme::px(6), playCenter.y())
            << QPointF(playCenter.x() - Theme::px(4), playCenter.y() + Theme::px(5));
    p.setBrush(m_session && m_session->isPlaying() ? Theme::accent() : Theme::accentAlt());
    p.setPen(Qt::NoPen);
    p.drawPolygon(playTri);

    p.setBrush(Theme::accent());
    p.drawRoundedRect(m_stopRect, Theme::px(2), Theme::px(2));

    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8, QFont::DemiBold));
    p.drawText(QRectF(transportRect.left() + Theme::px(44), transportRect.top(),
                      transportRect.width() - Theme::px(44), Theme::px(20)),
               Qt::AlignLeft | Qt::AlignVCenter, "PLAY/STOP");

    // Info left column.
    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(9, QFont::Bold));
    int infoY = static_cast<int>(infoLeft.top() + Theme::px(6));
    const int lineH = Theme::px(16);
    const int padIndex = m_pads ? m_pads->activePad() : 0;

    p.drawText(QRectF(infoLeft.left() + Theme::px(8), infoY,
                      infoLeft.width() - Theme::px(16), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, QString("ACTIVE PAD: %1").arg(padIndex + 1));
    infoY += lineH + 2;

    QString highlightName;
    SampleBrowserModel::Node *sel = m_browser.selected();
    if (sel && !sel->isDir) {
        highlightName = sel->name;
    }
    QFontMetrics infoFm(Theme::baseFont(9, QFont::Bold));
    const QString highlightText = infoFm.elidedText(highlightName, Qt::ElideRight, infoLeft.width() - 16);
    p.setPen(Theme::accentAlt());
    p.drawText(QRectF(infoLeft.left() + Theme::px(8), infoY,
                      infoLeft.width() - Theme::px(16), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, QString("HIGHLIGHT: %1").arg(highlightText));
    infoY += lineH + 2;

    const QString padFile = m_pads ? m_pads->padName(padIndex) : QString();
    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(9, QFont::Bold));
    p.drawText(QRectF(infoLeft.left() + Theme::px(8), infoY,
                      infoLeft.width() - Theme::px(16), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, "PAD FILE");
    infoY += lineH;
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8));
    const QString padText = padFile.isEmpty() ? "(empty)" : padFile;
    p.drawText(QRectF(infoLeft.left() + Theme::px(8), infoY,
                      infoLeft.width() - Theme::px(16), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, padText);
    infoY += lineH + 2;

    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(9, QFont::Bold));
    p.drawText(QRectF(infoLeft.left() + Theme::px(8), infoY,
                      infoLeft.width() - Theme::px(16), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, "STATUS");
    infoY += lineH;
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8));
    p.drawText(QRectF(infoLeft.left() + Theme::px(8), infoY,
                      infoLeft.width() - Theme::px(16), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, currentDirLabel());
    infoY += lineH + 2;

    if (m_session && !m_session->infoText().isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(8));
        p.drawText(QRectF(infoLeft.left() + Theme::px(8), infoY,
                          infoLeft.width() - Theme::px(16), lineH),
                   Qt::AlignLeft | Qt::AlignVCenter, m_session->infoText());
    }

    // Controls list.
    p.setFont(Theme::baseFont(8));
    p.setPen(Theme::textMuted());
    int controlsY = static_cast<int>(infoRight.top() + Theme::px(30));
    const QStringList controls = {
        "UP/DOWN = move",
        "ENTER = open folder",
        "BACKSPACE = up",
        "L = load to pad",
        "F = refresh",
        "SPACE = play/stop",
    };
    for (const QString &line : controls) {
        p.drawText(QRectF(infoRight.left() + Theme::px(8), controlsY,
                          infoRight.width() - Theme::px(16), lineH - Theme::px(2)),
                   Qt::AlignLeft | Qt::AlignVCenter, line);
        controlsY += lineH - Theme::px(2);
        if (controlsY > infoRight.bottom() - Theme::px(18)) {
            break;
        }
    }

    if (m_session && !m_session->errorText().isEmpty()) {
        p.setPen(Theme::danger());
        p.setFont(Theme::baseFont(8, QFont::DemiBold));
        p.drawText(QRectF(infoRight.left() + Theme::px(8),
                          infoRight.bottom() - Theme::px(16),
                          infoRight.width() - Theme::px(16), Theme::px(14)),
                   Qt::AlignLeft | Qt::AlignVCenter, m_session->errorText());
    }

    // Idle ambience (VIDEO_06).
    if (!lite && (!m_session || !m_session->isPlaying())) {
        Theme::drawIdleDust(p, rect(), 0.06f);
    }
}
