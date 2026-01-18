#include "ConsoleModeGuard.h"

#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

ConsoleModeGuard::ConsoleModeGuard() {
#ifdef Q_OS_LINUX
    m_fd = ::open("/dev/tty", O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        return;
    }

    int mode = 0;
    if (::ioctl(m_fd, KDGETMODE, &mode) != 0) {
        return;
    }
    m_prevMode = mode;

    if (mode != KD_GRAPHICS) {
        if (::ioctl(m_fd, KDSETMODE, KD_GRAPHICS) != 0) {
            return;
        }
    }

    const char hideCursor[] = "\033[?25l";
    ::write(m_fd, hideCursor, sizeof(hideCursor) - 1);
    m_active = true;
#endif
}

ConsoleModeGuard::~ConsoleModeGuard() {
#ifdef Q_OS_LINUX
    if (m_fd < 0) {
        return;
    }
    if (m_active && m_prevMode != -1) {
        const char showCursor[] = "\033[?25h";
        ::write(m_fd, showCursor, sizeof(showCursor) - 1);
        ::ioctl(m_fd, KDSETMODE, m_prevMode);
    }
    ::close(m_fd);
#endif
}

bool ConsoleModeGuard::isActive() const {
#ifdef Q_OS_LINUX
    return m_active;
#else
    return false;
#endif
}
