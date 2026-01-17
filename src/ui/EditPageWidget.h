#pragma once

#include <QString>
#include <QVector>
#include <QWidget>

class QPaintEvent;

class SampleSession;

class EditPageWidget : public QWidget {
    Q_OBJECT
public:
    explicit EditPageWidget(SampleSession *session, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Param {
        QString label;
        float value = 0.0f;
    };

    QVector<Param> m_params;
    SampleSession *m_session = nullptr;
};
