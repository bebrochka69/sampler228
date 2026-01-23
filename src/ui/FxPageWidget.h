#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QShowEvent;
class PadBank;

class FxPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit FxPageWidget(PadBank *pads, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void syncBusEffects(int trackIndex);

    struct Track {
        QString name;
        QVector<QString> inserts;
    };

    struct SlotHit {
        QRectF rect;
        int track = 0;
        int slot = 0;
    };

    struct EffectHit {
        QRectF rect;
        int index = 0;
    };

    void assignEffect(int effectIndex);
    void swapSlot(int track, int a, int b);

    QVector<Track> m_tracks;
    QStringList m_effects;
    PadBank *m_pads = nullptr;

    int m_selectedTrack = 0;
    int m_selectedSlot = 0;
    int m_selectedEffect = 0;
    bool m_showMenu = false;

    QVector<SlotHit> m_slotHits;
    QVector<EffectHit> m_effectHits;
};
