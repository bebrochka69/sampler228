#pragma once

#include <QColor>
#include <QFont>
#include <QFontInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QtGlobal>

namespace Theme {
inline QColor bg0() { return QColor(12, 14, 22); }
inline QColor bg1() { return QColor(24, 30, 46); }
inline QColor bg2() { return QColor(16, 20, 34); }
inline QColor bg3() { return QColor(34, 42, 66); }
inline QColor stroke() { return QColor(86, 132, 230); }
inline QColor accent() { return QColor(90, 236, 255); }
inline QColor accentAlt() { return QColor(255, 186, 96); }
inline QColor text() { return QColor(230, 238, 248); }
inline QColor textMuted() { return QColor(150, 168, 204); }
inline QColor warn() { return QColor(255, 206, 120); }
inline QColor danger() { return QColor(255, 108, 108); }

inline QFont baseFont(int pt, QFont::Weight weight = QFont::Normal) {
    QFont f("DejaVu Sans Mono");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans");
    }
    f.setPointSize(pt);
    f.setWeight(weight);
    return f;
}

inline QFont condensedFont(int pt, QFont::Weight weight = QFont::DemiBold) {
    QFont f("DejaVu Sans");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans");
    }
    f.setPointSize(pt);
    f.setWeight(weight);
    return f;
}

inline QColor withAlpha(const QColor &c, int alpha) {
    QColor out = c;
    out.setAlpha(alpha);
    return out;
}

inline bool liteMode() {
    return qEnvironmentVariableIsSet("GROOVEBOX_LITE");
}

inline void paintBackground(QPainter &p, const QRectF &rect) {
    QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
    grad.setColorAt(0.0, bg0());
    grad.setColorAt(1.0, bg2());
    p.fillRect(rect, grad);

    // Subtle scanlines for a CRT feel.
    if (!liteMode()) {
        p.setPen(QPen(withAlpha(stroke(), 20), 1.0));
        for (int y = 0; y < rect.height(); y += 6) {
            const qreal yy = rect.top() + y;
            p.drawLine(QPointF(rect.left(), yy), QPointF(rect.right(), yy));
        }
    }
}
}
