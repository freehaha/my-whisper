#include "app_controller.h"

#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("MyWhisperQt");
    QApplication::setQuitOnLastWindowClosed(false);

    AppController controller;
    return app.exec();
}
