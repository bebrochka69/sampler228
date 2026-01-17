#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;

class EditPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit EditPageWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Param {
        QString label;
        float value = 0.0f;
    };

    QVector<Param> m_params;
    QVector<float> m_wave;
};
