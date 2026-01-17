#include "FramebufferCleaner.h"

#include <QGuiApplication>
#include <QString>

#ifdef Q_OS_LINUX
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

void FramebufferCleaner::clearIfNeeded() {
#ifdef Q_OS_LINUX
    const QString platform = QGuiApplication::platformName();
    if (!platform.contains("linuxfb") && !platform.contains("eglfs") &&
        !platform.contains("vkkhrdisplay")) {
        return;
    }

    const int fd = ::open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        return;
    }

    fb_fix_screeninfo finfo {};
    fb_var_screeninfo vinfo {};
    if (::ioctl(fd, FBIOGET_FSCREENINFO, &finfo) != 0 ||
        ::ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) != 0) {
        ::close(fd);
        return;
    }

    const size_t screenSize = static_cast<size_t>(vinfo.yres_virtual) * finfo.line_length;
    void *data = ::mmap(nullptr, screenSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        ::close(fd);
        return;
    }

    std::memset(data, 0, screenSize);
    ::msync(data, screenSize, MS_SYNC);
    ::munmap(data, screenSize);
    ::close(fd);
#endif
}
