#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QtGlobal>

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

    FramebufferCleaner::clearIfNeeded();

    MainWindow window;
    window.resize(1280, 720);

    const QString platform = QGuiApplication::platformName();
    if (isFramebufferPlatform(platform)) {
        window.setWindowFlags(Qt::FramelessWindowHint);
        window.showFullScreen();
    } else {
        window.setFixedSize(1280, 720);
        window.show();
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        FramebufferCleaner::clearIfNeeded();
    });

    return app.exec();
}
