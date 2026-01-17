#pragma once

#include <QString>
#include <QWidget>

class QPaintEvent;

class SimplePageWidget : public QWidget {
    Q_OBJECT
public:
    explicit SimplePageWidget(const QString &title, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
};
