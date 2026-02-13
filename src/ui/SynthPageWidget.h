#pragma once

#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QPaintEvent;
class QMouseEvent;
class QKeyEvent;
class PadBank;

class SynthPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SynthPageWidget(PadBank *pads, QWidget *parent = nullptr);

    void setActivePad(int pad);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct PresetRow {
        QString label;
        QString presetId;
        bool header = false;
        QRectF rect;
    };

    struct EditParam {
        QString label;
        int type = 0;
        QRectF rect;
    };

    void reloadBanks(bool syncSelection);
    void adjustEditParam(int delta);
    float currentEditValue(const EditParam &param) const;

    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    QStringList m_fluidPresets;
    QVector<PresetRow> m_presetRows;
    QVector<EditParam> m_editParams;
    int m_selectedEditParam = 0;
    QStringList m_categories;
    QVector<QRectF> m_categoryRects;
    QVector<QRectF> m_filterPresetRects;
    int m_selectedCategory = 0;
    int m_presetScroll = 0;
    bool m_showPresetMenu = false;
    QRectF m_presetButtonRect;
    QRectF m_presetPanelRect;
};
