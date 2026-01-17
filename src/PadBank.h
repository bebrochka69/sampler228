#pragma once

#include <QObject>
#include <QString>
#include <array>

class PadBank : public QObject {
    Q_OBJECT
public:
    explicit PadBank(QObject *parent = nullptr);

    int padCount() const { return static_cast<int>(m_paths.size()); }
    int activePad() const { return m_activePad; }
    void setActivePad(int index);

    void setPadPath(int index, const QString &path);
    QString padPath(int index) const;
    QString padName(int index) const;
    bool isLoaded(int index) const;

signals:
    void padChanged(int index);
    void activePadChanged(int index);

private:
    std::array<QString, 8> m_paths;
    int m_activePad = 0;
};
