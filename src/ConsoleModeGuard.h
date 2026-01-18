#pragma once

class ConsoleModeGuard {
public:
    ConsoleModeGuard();
    ~ConsoleModeGuard();

    bool isActive() const;

private:
#ifdef Q_OS_LINUX
    int m_fd = -1;
    int m_prevMode = -1;
    bool m_active = false;
#endif
};
