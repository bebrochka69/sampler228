#pragma once

#include <QRectF>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <QTimer>

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
    enum class EditorMode {
        None,
        Lfo,
        Env,
        Filter
    };

    enum class BindSource {
        None,
        Lfo,
        Env
    };

    struct PresetRow {
        QString label;
        QString presetId;
        QString bank;
        bool header = false;
        QRectF rect;
    };

    struct PresetEntry {
        QString preset;
        QString bank;
        QString category;
        QString label;
    };

    struct EditParam {
        QString label;
        int type = 0;
        QRectF rect;
    };

    struct EditorHit {
        QRectF rect;
        QString id;
        int slot = -1;
        int value = 0;
    };

    void reloadBanks(bool syncSelection);
    void adjustEditParam(int delta);
    float currentEditValue(const EditParam &param) const;
    int modTargetForParam(int paramType, const QString &synthType) const;

    PadBank *m_pads = nullptr;
    int m_activePad = 0;
    QVector<PresetEntry> m_allPresets;
    QVector<PresetRow> m_presetRows;
    QVector<EditParam> m_editParams;
    int m_selectedEditParam = 0;
    QStringList m_categories;
    QVector<QRectF> m_categoryRects;
    QVector<QRectF> m_filterPresetRects;
    int m_selectedCategory = 0;
    int m_presetScroll = 0;
    bool m_showPresetMenu = false;
    bool m_modMenuOpen = false;
    int m_modTab = 0;
    QRectF m_modMenuRect;
    QVector<QRectF> m_modTabRects;
    QVector<QRectF> m_modParamRects;
    bool m_assignMenuOpen = false;
    int m_assignParamType = -1;
    QRectF m_assignRect;
    QRectF m_assignLfoRect;
    QRectF m_assignEnvRect;
    QTimer m_holdTimer;
    bool m_holdActive = false;
    int m_holdParamType = -1;
    QPointF m_holdPos;
    EditorMode m_editorMode = EditorMode::None;
    QVector<EditorHit> m_editorHits;
    QRectF m_editorContentRect;
    QRectF m_editorLeftRect;
    QRectF m_editorRightRect;
    QRectF m_editorAddRect;
    int m_lfoScroll = 0;
    int m_envScroll = 0;
    int m_filterScroll = 0;
    int m_activeLfoSlot = 0;
    int m_activeEnvSlot = 0;
    int m_activeFilterSlot = 0;
    BindSource m_bindSource = BindSource::None;
    EditorMode m_bindReturnMode = EditorMode::None;
    int m_bindSlot = -1;
    QRectF m_presetButtonRect;
    QRectF m_presetPanelRect;
    QRectF m_busRect;
};
