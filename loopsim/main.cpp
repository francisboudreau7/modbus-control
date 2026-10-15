// modbus_loop_sim: level loop 101 simulator (window + LIC-101 Modbus TCP server).
// Stub created by T210; T212 adds the command line and --headless, T214 the window details.

#include "LoopController.h"
#include "LoopWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Modbus Loop Simulator"));
    mbloop::LoopController controller;
    QString error;
    controller.startServer(QStringLiteral("127.0.0.1"), 1601, &error);
    mbloop::LoopWindow window(&controller);
    window.show();
    return app.exec();
}
