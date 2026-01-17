#include <QApplication>
#include <QCoreApplication>

#include "FramebufferCleaner.h"
#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("GrooveBoxUI");
    app.setOrganizationName("GrooveBox");

    MainWindow window;
    window.show();

    QObject::connect(&app, &QCoreApplication::aboutToQuit, []() {
        FramebufferCleaner::clearIfNeeded();
    });

    return app.exec();
}
