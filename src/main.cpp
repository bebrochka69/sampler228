#include <QApplication>

#include "MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("GrooveBoxUI");
    app.setOrganizationName("GrooveBox");

    MainWindow window;
    window.show();

    return app.exec();
}
