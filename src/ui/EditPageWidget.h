#pragma once

#include <QHash>
#include <QPixmap>
#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;
class QKeyEvent;
class QPixmap;

class SampleSession;
class PadBank;

class EditPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit EditPageWidget(SampleSession *session, PadBank *pads, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    struct Param {
        QString label;
        enum Type {
            Volume,
            Pan,
            Pitch,
            Stretch,
            Start,
            End,
            Slice,
            Mode
        } type;
    };

    QVector<Param> m_params;
    SampleSession *m_session = nullptr;
    PadBank *m_pads = nullptr;
    int m_selectedParam = 0;
    QHash<int, QPixmap> m_iconCache;
    QRectF m_fxBusRect;
    QRectF m_normalizeRect;
    QVector<QRectF> m_paramRects;

    void syncWaveSource();
    QString iconFileFor(Param::Type type) const;
    QPixmap iconForType(Param::Type type);
};
