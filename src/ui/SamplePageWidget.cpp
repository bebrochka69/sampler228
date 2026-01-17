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
#include "WaveformRenderer.h"

SamplePageWidget::SamplePageWidget(SampleSession *session, PadBank *pads, QWidget *parent)
    : QWidget(parent), m_session(session), m_pads(pads) {
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

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
    const int rowHeight = 24;
    const int totalHeight = m_entries.size() * rowHeight;
    const int viewHeight = height() - 120;
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
        m_session->setSource(node->path);
    }

    const int rowHeight = 24;
    const int listTop = 84;
    const int viewHeight = height() - 120;
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

    const int leftWidth = static_cast<int>(width() * 0.6f);
    const QRectF listRect(12, 84, leftWidth - 24, height() - 120);
    if (!listRect.contains(pos)) {
        return;
    }

    const int rowHeight = 24;
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
        m_session->setSource(node->path);
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
    p.setRenderHint(QPainter::Antialiasing, true);

    p.fillRect(rect(), Theme::bg0());

    const int leftWidth = static_cast<int>(width() * 0.6f);
    const QRectF leftRect(0, 0, leftWidth, height());
    const QRectF rightRect(leftWidth, 0, width() - leftWidth, height());

    // Left header.
    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(13, QFont::Bold));
    p.drawText(QRectF(12, 10, leftRect.width() - 24, 20), Qt::AlignLeft | Qt::AlignVCenter,
               "WELCOME // USB SAMPLE BROWSER");

    const QRectF dirRect(12, 36, leftRect.width() - 24, 26);
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.setBrush(Theme::bg1());
    p.drawRect(dirRect);

    QFont dirFont = Theme::baseFont(9, QFont::DemiBold);
    p.setFont(dirFont);
    p.setPen(Theme::text());
    QFontMetrics dirFm(dirFont);
    const QString dirText = dirFm.elidedText(currentDirLabel(), Qt::ElideRight, dirRect.width() - 12);
    p.drawText(dirRect.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter, dirText);

    m_rescanRect = QRectF(dirRect.right() - 28, dirRect.top() + 4, 18, 18);
    p.setPen(QPen(Theme::accent(), 1.0));
    p.setBrush(Theme::bg2());
    p.drawRect(m_rescanRect);
    p.setFont(Theme::baseFont(9, QFont::Bold));
    p.drawText(m_rescanRect, Qt::AlignCenter, "R");

    // Browser list.
    const QRectF listRect(12, 84, leftRect.width() - 24, height() - 120);
    p.setPen(QPen(Theme::stroke(), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(listRect);

    p.save();
    p.setClipRect(listRect.adjusted(2, 2, -2, -2));

    const int rowHeight = 24;
    int y = static_cast<int>(listRect.top()) - (m_scrollOffset % rowHeight);
    const int startIndex = qMax(0, m_scrollOffset / rowHeight);

    QFont rowFont = Theme::baseFont(10);
    p.setFont(rowFont);

    if (m_entries.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.drawText(listRect, Qt::AlignCenter, "No USB media");
    } else {
        for (int i = startIndex; i < m_entries.size(); ++i) {
            if (y > listRect.bottom()) {
                break;
            }

            const SampleBrowserModel::Entry &entry = m_entries[i];
            const QRectF row(listRect.left() + 4, y, listRect.width() - 8, rowHeight - 2);

            const bool selected = (i == m_selectedIndex);
            p.setPen(QPen(Theme::stroke(), 1.0));
            p.setBrush(selected ? Theme::accentAlt() : Theme::bg1());
            p.drawRect(row);

            const float indent = entry.depth * 12.0f;
            QString label;
            if (entry.node && entry.node->isDir) {
                label = QString("[DIR] %1").arg(entry.node->name);
            } else if (entry.node) {
                label = entry.node->name;
            }

            p.setPen(selected ? Theme::bg2() : Theme::text());
            p.drawText(QRectF(row.left() + 6 + indent, row.top(), row.width() - 8, row.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, label);

            y += rowHeight;
        }
    }

    p.restore();

    // Right panels.
    const QRectF projectsRect(rightRect.left() + 12, 18, rightRect.width() - 24, height() * 0.26f);
    p.setPen(QPen(Theme::accent(), 1.2));
    p.setBrush(Theme::bg2());
    p.drawRect(projectsRect);

    p.setPen(Theme::accent());
    p.setFont(Theme::condensedFont(12, QFont::Bold));
    p.drawText(projectsRect.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignTop, "PROJECTS");

    p.setFont(Theme::baseFont(9));
    p.setPen(Theme::text());
    int py = static_cast<int>(projectsRect.top() + 28);
    if (m_projects.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(projectsRect.left() + 8, py, projectsRect.width() - 16, 16),
                   Qt::AlignLeft | Qt::AlignVCenter, "No projects");
    } else {
        for (int i = 0; i < m_projects.size(); ++i) {
            QRectF row(projectsRect.left() + 8, py, projectsRect.width() - 16, 16);
            p.drawText(row, Qt::AlignLeft | Qt::AlignVCenter, m_projects[i]);
            py += 18;
            if (py > projectsRect.bottom() - 14) {
                break;
            }
        }
    }

    const QRectF sampleRect(rightRect.left() + 12, projectsRect.bottom() + 14,
                            rightRect.width() - 24, height() - projectsRect.bottom() - 28);
    p.setPen(QPen(Theme::accentAlt(), 1.2));
    p.setBrush(Theme::bg2());
    p.drawRect(sampleRect);

    const QRectF waveRect(sampleRect.left() + 10, sampleRect.top() + 10,
                          sampleRect.width() * 0.62f, sampleRect.height() - 50);
    const QRectF infoRect(waveRect.right() + 10, sampleRect.top() + 10,
                          sampleRect.right() - waveRect.right() - 20, sampleRect.height() - 20);

    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg1());
    p.drawRect(waveRect);

    QVector<float> wave;
    if (m_session && m_session->hasWaveform()) {
        wave = m_session->waveform();
    }
    if (wave.isEmpty()) {
        p.setPen(Theme::textMuted());
        p.setFont(Theme::baseFont(10, QFont::DemiBold));
        p.drawText(waveRect, Qt::AlignCenter, "NO SAMPLE");
    } else {
        WaveformRenderer::drawWaveform(p, waveRect.adjusted(4, 4, -4, -4), wave, Theme::accent(),
                                       Theme::withAlpha(Theme::textMuted(), 140));
    }

    // Preview controls.
    const QRectF controlsRect(sampleRect.left() + 10, sampleRect.bottom() - 32, 120, 22);
    p.setPen(QPen(Theme::stroke(), 1.0));
    p.setBrush(Theme::bg1());
    p.drawRect(controlsRect);

    m_playRect = QRectF(controlsRect.left() + 10, controlsRect.top() + 4, 12, 12);
    m_stopRect = QRectF(controlsRect.left() + 34, controlsRect.top() + 4, 12, 12);

    const QPointF playCenter(m_playRect.center());
    QPolygonF playTri;
    playTri << QPointF(playCenter.x() - 4, playCenter.y() - 5)
            << QPointF(playCenter.x() + 6, playCenter.y())
            << QPointF(playCenter.x() - 4, playCenter.y() + 5);
    p.setBrush(m_session && m_session->isPlaying() ? Theme::accent() : Theme::accentAlt());
    p.setPen(Qt::NoPen);
    p.drawPolygon(playTri);

    p.setBrush(Theme::accent());
    p.drawRect(m_stopRect);

    // Info panel.
    p.setPen(Theme::accent());
    p.setFont(Theme::baseFont(9, QFont::Bold));

    int infoY = static_cast<int>(infoRect.top());
    const int lineH = 16;
    const int padIndex = m_pads ? m_pads->activePad() : 0;
    const QString padLabel = QString("PAD %1").arg(padIndex + 1);

    p.drawText(QRectF(infoRect.left(), infoY, infoRect.width(), lineH), Qt::AlignLeft | Qt::AlignVCenter,
               padLabel);
    infoY += lineH + 4;

    p.setPen(Theme::text());
    p.drawText(QRectF(infoRect.left(), infoY, infoRect.width(), lineH), Qt::AlignLeft | Qt::AlignVCenter,
               "HIGHLIGHT:");
    infoY += lineH;

    QString highlightName;
    SampleBrowserModel::Node *sel = m_browser.selected();
    if (sel && !sel->isDir) {
        highlightName = sel->name;
    }
    QFontMetrics infoFm(Theme::baseFont(9, QFont::Bold));
    const QString highlightText = infoFm.elidedText(highlightName, Qt::ElideRight, infoRect.width());
    p.setPen(Theme::accentAlt());
    p.drawText(QRectF(infoRect.left(), infoY, infoRect.width(), lineH), Qt::AlignLeft | Qt::AlignVCenter,
               highlightText);
    infoY += lineH + 6;

    p.setPen(Theme::text());
    p.drawText(QRectF(infoRect.left(), infoY, infoRect.width(), lineH), Qt::AlignLeft | Qt::AlignVCenter,
               "CONTROLS:");
    infoY += lineH;

    p.setFont(Theme::baseFont(8));
    p.setPen(Theme::textMuted());
    const QStringList controls = {
        "UP/DOWN = move",
        "ENTER = open folder",
        "BACKSPACE = up",
        "L = load to pad",
        "F = refresh",
        "SPACE = play/stop",
    };
    for (const QString &line : controls) {
        p.drawText(QRectF(infoRect.left(), infoY, infoRect.width(), lineH - 2),
                   Qt::AlignLeft | Qt::AlignVCenter, line);
        infoY += lineH - 2;
        if (infoY > infoRect.bottom() - 60) {
            break;
        }
    }

    p.setFont(Theme::baseFont(9, QFont::Bold));
    p.setPen(Theme::text());
    p.drawText(QRectF(infoRect.left(), infoRect.bottom() - 52, infoRect.width(), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, "STATUS:");
    p.setFont(Theme::baseFont(8));
    p.setPen(Theme::textMuted());
    p.drawText(QRectF(infoRect.left(), infoRect.bottom() - 36, infoRect.width(), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, currentDirLabel());

    const QString padFile = m_pads ? m_pads->padName(padIndex) : QString();
    p.setPen(Theme::text());
    p.setFont(Theme::baseFont(9, QFont::Bold));
    p.drawText(QRectF(infoRect.left(), infoRect.bottom() - 20, infoRect.width(), lineH),
               Qt::AlignLeft | Qt::AlignVCenter, "PAD FILE:");
    p.setPen(Theme::textMuted());
    p.setFont(Theme::baseFont(8));
    const QString padText = padFile.isEmpty() ? "(empty)" : padFile;
    p.drawText(QRectF(infoRect.left() + 70, infoRect.bottom() - 20, infoRect.width() - 70, lineH),
               Qt::AlignLeft | Qt::AlignVCenter, padText);

    if (m_session && !m_session->errorText().isEmpty()) {
        p.setPen(Theme::danger());
        p.setFont(Theme::baseFont(8, QFont::DemiBold));
        p.drawText(QRectF(infoRect.left(), infoRect.bottom() - 36, infoRect.width(), lineH),
                   Qt::AlignLeft | Qt::AlignVCenter, m_session->errorText());
    }

    // Sample info line.
    if (m_session && !m_session->infoText().isEmpty()) {
        p.setFont(Theme::baseFont(8));
        p.setPen(Theme::textMuted());
        p.drawText(QRectF(waveRect.left(), waveRect.bottom() + 6, waveRect.width(), 14),
                   Qt::AlignLeft | Qt::AlignVCenter, m_session->infoText());
    }
}
