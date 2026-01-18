#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QTimer>
#include <QtGlobal>
#include <cstdio>
#include <memory>
#include <csignal>

#include "ConsoleModeGuard.h"
#include "FramebufferCleaner.h"
#include "MainWindow.h"

namespace {
class ExitShortcutFilter : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override {
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            const bool ctrl = keyEvent->modifiers().testFlag(Qt::ControlModifier);
            const int key = keyEvent->key();
            if (key == Qt::Key_Escape || key == Qt::Key_F12 ||
                (ctrl && (key == Qt::Key_C || key == Qt::Key_Q))) {
                QCoreApplication::quit();
                return true;
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

bool isFramebufferPlatform(const QString &platform) {
    return platform.contains("linuxfb") || platform.contains("eglfs") ||
           platform.contains("vkkhrdisplay");
}

volatile std::sig_atomic_t g_sigintRequested = 0;

void handleSigint(int) {
    g_sigintRequested = 1;
}
}  // namespace

int main(int argc, char *argv[]) {
#ifdef Q_OS_LINUX
    if (qEnvironmentVariableIsEmpty("DISPLAY") &&
        qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY") &&
        qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArray("linuxfb"));
    }
#endif

    QApplication app(argc, argv);
    app.setApplicationName("GrooveBoxUI");
    app.setOrganizationName("GrooveBox");
    app.setQuitOnLastWindowClosed(true);

    ExitShortcutFilter exitFilter;
    app.installEventFilter(&exitFilter);

    std::signal(SIGINT, handleSigint);
    QTimer sigintTimer;
    sigintTimer.setInterval(100);
    QObject::connect(&sigintTimer, &QTimer::timeout, []() {
        if (g_sigintRequested) {
            QCoreApplication::quit();
        }
    });
    sigintTimer.start();

    const QString platform = QGuiApplication::platformName();
    if (isFramebufferPlatform(platform) && qEnvironmentVariableIsEmpty("GROOVEBOX_LITE")) {
        qputenv("GROOVEBOX_LITE", QByteArray("1"));
    }
    std::unique_ptr<ConsoleModeGuard> consoleGuard;
    if (isFramebufferPlatform(platform)) {
        consoleGuard = std::make_unique<ConsoleModeGuard>();
        if (qEnvironmentVariableIsEmpty("GROOVEBOX_KEEP_CONSOLE")) {
            std::freopen("/dev/null", "w", stdout);
            std::freopen("/dev/null", "w", stderr);
        }
    }

    FramebufferCleaner::clearIfNeeded();

    MainWindow window;
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QRect screenRect = screen ? screen->geometry() : QRect(0, 0, 1280, 720);
    window.setGeometry(screenRect);

    if (isFramebufferPlatform(platform)) {
        window.setWindowFlags(Qt::FramelessWindowHint);
        window.showFullScreen();
    } else {
        window.resize(1280, 720);
        window.show();
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        FramebufferCleaner::clearIfNeeded();
    });

    return app.exec();
}
