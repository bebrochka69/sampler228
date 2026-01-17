#pragma once

#include <QColor>
#include <QFont>
#include <QFontInfo>

namespace Theme {
inline QColor bg0() { return QColor(15, 17, 22); }
inline QColor bg1() { return QColor(24, 27, 34); }
inline QColor bg2() { return QColor(34, 38, 48); }
inline QColor bg3() { return QColor(46, 51, 64); }
inline QColor stroke() { return QColor(70, 77, 92); }
inline QColor accent() { return QColor(238, 86, 65); }
inline QColor accentAlt() { return QColor(63, 198, 179); }
inline QColor text() { return QColor(235, 237, 240); }
inline QColor textMuted() { return QColor(165, 170, 182); }
inline QColor warn() { return QColor(255, 120, 30); }
inline QColor danger() { return QColor(255, 70, 70); }

inline QFont baseFont(int pt, QFont::Weight weight = QFont::Normal) {
    QFont f("Noto Sans");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans");
    }
    f.setPointSize(pt);
    f.setWeight(weight);
    return f;
}

inline QFont condensedFont(int pt, QFont::Weight weight = QFont::DemiBold) {
    QFont f("Noto Sans Condensed");
    if (!QFontInfo(f).exactMatch()) {
        f = QFont("DejaVu Sans Condensed");
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
}
