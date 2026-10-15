// modbus_loop_sim: level loop 101 simulator (window + LIC-101 Modbus TCP server).

#include "LoopController.h"
#include "LoopWindow.h"
#include "Scenario.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMessageBox>
#include <QString>
#include <QTimer>

#include <modbus.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#endif

#ifndef MBLOOP_VERSION
#define MBLOOP_VERSION "0.0.0"
#endif

namespace {

std::atomic<bool> g_stop{false};

#ifdef _WIN32
BOOL WINAPI consoleHandler(DWORD ctrlType)
{
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        g_stop = true;
        if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT) {
            return TRUE;
        }
        Sleep(1000); // Windows kills the process shortly after we return
        return TRUE;
    default:
        return FALSE;
    }
}

/// The executable is a GUI-subsystem program: connect stdout/stderr to the parent console when
/// they are not redirected, so --help, --version and --headless print there.
void attachParentConsole()
{
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        return; // redirected to a file or pipe
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}
#else
void signalHandler(int)
{
    g_stop = true;
}
void attachParentConsole() {}
#endif

bool g_headless = false;

/// Reports a fatal command-line / startup problem: stderr when headless, a message box otherwise.
int fail(const QString &message, int code)
{
    if (g_headless) {
        std::fprintf(stderr, "modbus_loop_sim: %s\n", qPrintable(message));
    } else {
        QMessageBox::critical(nullptr, QStringLiteral("Modbus Loop Simulator"), message);
    }
    return code;
}

} // namespace

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0) {
            g_headless = true;
        }
    }
    attachParentConsole();

    std::unique_ptr<QCoreApplication> app;
    if (g_headless) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    } else {
        app = std::make_unique<QApplication>(argc, argv);
    }
    QCoreApplication::setApplicationName(QStringLiteral("Modbus Loop Simulator"));
    QCoreApplication::setApplicationVersion(QStringLiteral(MBLOOP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Level loop 101 simulator with the LIC-101 regulator as a Modbus TCP server"));
    const QCommandLineOption helpOption({QStringLiteral("h"), QStringLiteral("help")}, QStringLiteral("Show this help."));
    const QCommandLineOption versionOption(QStringLiteral("version"), QStringLiteral("Show version."));
    const QCommandLineOption portOption(QStringLiteral("port"),
                                        QStringLiteral("TCP port, 0 = let the OS choose (default 1601)."),
                                        QStringLiteral("port"));
    const QCommandLineOption hostOption(QStringLiteral("host"),
                                        QStringLiteral("Address to listen on (default 127.0.0.1; use 0.0.0.0 to accept remote clients)."),
                                        QStringLiteral("address"));
    const QCommandLineOption scenarioOption(QStringLiteral("scenario"), QStringLiteral("Scenario file (.mbloop) to load."),
                                            QStringLiteral("file"));
    const QCommandLineOption speedOption(QStringLiteral("speed"), QStringLiteral("Simulation speed factor: 1, 2, 5 or 10."),
                                         QStringLiteral("factor"));
    const QCommandLineOption headlessOption(QStringLiteral("headless"),
                                            QStringLiteral("No window: run the loop and the Modbus server only."));
    parser.addOptions({helpOption, versionOption, portOption, hostOption, scenarioOption, speedOption, headlessOption});

    if (!parser.parse(QCoreApplication::arguments())) {
        return fail(parser.errorText(), 2);
    }
    if (parser.isSet(versionOption)) {
        std::printf("modbus_loop_sim %s (libmodbus %s)\n", qPrintable(QCoreApplication::applicationVersion()),
                    LIBMODBUS_VERSION_STRING);
        return 0;
    }
    if (parser.isSet(helpOption)) {
        std::printf("%s", qPrintable(parser.helpText()));
        return 0;
    }
    if (!parser.positionalArguments().isEmpty()) {
        return fail(QStringLiteral("unexpected argument '%1'").arg(parser.positionalArguments().first()), 2);
    }

    bool ok = true;
    quint16 port = 1601;
    if (parser.isSet(portOption)) {
        const int p = parser.value(portOption).toInt(&ok);
        if (!ok || p < 0 || p > 65535) {
            return fail(QStringLiteral("--port must be 0..65535"), 2);
        }
        port = static_cast<quint16>(p);
    }
    const QString host = parser.isSet(hostOption) ? parser.value(hostOption) : QStringLiteral("127.0.0.1");
    double speed = 0.0;
    if (parser.isSet(speedOption)) {
        const int s = parser.value(speedOption).toInt(&ok);
        if (!ok || (s != 1 && s != 2 && s != 5 && s != 10)) {
            return fail(QStringLiteral("--speed must be 1, 2, 5 or 10"), 2);
        }
        speed = s;
    }

    mbloop::LoopController controller;
    if (parser.isSet(scenarioOption)) {
        QString error;
        const auto scenario = mbloop::loadScenario(parser.value(scenarioOption), &error);
        if (!scenario) {
            return fail(error, 2);
        }
        controller.loadScenario(*scenario);
    }
    if (speed > 0.0) {
        controller.setSpeedFactor(speed);
    }

    QString serverError;
    const bool serverOk = controller.startServer(host, port, &serverError);

    if (g_headless) {
        if (!serverOk) {
            return fail(serverError, 1);
        }
#ifdef _WIN32
        SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
        std::signal(SIGINT, signalHandler);
        std::signal(SIGTERM, signalHandler);
#endif
        std::printf("Listening on %s:%u\n", qPrintable(host), static_cast<unsigned>(controller.server().port()));
        std::fflush(stdout);
        QTimer stopPoll;
        QObject::connect(&stopPoll, &QTimer::timeout, app.get(), [&] {
            if (g_stop.load()) {
                QCoreApplication::quit();
            }
        });
        stopPoll.start(100);
        return app->exec();
    }

    mbloop::LoopWindow window(&controller);
    window.setServerStatus(serverOk ? QStringLiteral("Modbus server on %1:%2")
                                          .arg(host)
                                          .arg(controller.server().port())
                                    : serverError);
    window.show();
    return app->exec();
}
