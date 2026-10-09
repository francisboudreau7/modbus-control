#include "ServerConfig.h"
#include "ServerLoop.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QString>

#include <modbus.h>

#include <atomic>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
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
        // For close/logoff/shutdown Windows kills the process shortly after we return;
        // give the main loop a moment to clean up.
        Sleep(1000);
        return TRUE;
    default:
        return FALSE;
    }
}
#else
void signalHandler(int)
{
    g_stop = true;
}
#endif

int usageError(const QString &message)
{
    std::fprintf(stderr, "modbus_test_server: %s\n", qPrintable(message));
    return 2;
}

const char *kindName(mbsim::SignalKind kind)
{
    switch (kind) {
    case mbsim::SignalKind::Constant: return "constant";
    case mbsim::SignalKind::Sine: return "sine";
    case mbsim::SignalKind::Triangle: return "triangle";
    case mbsim::SignalKind::Sawtooth: return "sawtooth";
    case mbsim::SignalKind::Square: return "square";
    case mbsim::SignalKind::Counter: return "counter";
    case mbsim::SignalKind::Noise: return "noise";
    case mbsim::SignalKind::Lag: return "lag";
    }
    return "?";
}

const char *formatName(mbsim::RegisterFormat format)
{
    switch (format) {
    case mbsim::RegisterFormat::Int16: return "int16";
    case mbsim::RegisterFormat::Int32: return "int32";
    case mbsim::RegisterFormat::Float32: return "float32";
    }
    return "?";
}

const char *orderName(mbsim::WordOrder order)
{
    switch (order) {
    case mbsim::WordOrder::ABCD: return "ABCD";
    case mbsim::WordOrder::CDAB: return "CDAB";
    case mbsim::WordOrder::BADC: return "BADC";
    case mbsim::WordOrder::DCBA: return "DCBA";
    }
    return "?";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("modbus_test_server"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Modbus TCP test server with simulated signals and fault injection"));
    const QCommandLineOption helpOption({QStringLiteral("h"), QStringLiteral("help")}, QStringLiteral("Show this help."));
    const QCommandLineOption versionOption(QStringLiteral("version"), QStringLiteral("Show version."));
    const QCommandLineOption configOption(QStringLiteral("config"), QStringLiteral("JSON configuration file."), QStringLiteral("file"));
    const QCommandLineOption hostOption(QStringLiteral("host"), QStringLiteral("Address to listen on (default 127.0.0.1; use --host 0.0.0.0 to accept remote clients)."), QStringLiteral("address"));
    const QCommandLineOption portOption(QStringLiteral("port"), QStringLiteral("TCP port, 0 = let the OS choose (default 1502)."), QStringLiteral("port"));
    const QCommandLineOption unitOption(QStringLiteral("unit"), QStringLiteral("Modbus unit id (default 1)."), QStringLiteral("id"));
    const QCommandLineOption tickOption(QStringLiteral("tick"), QStringLiteral("Simulation tick period in ms (default 50)."), QStringLiteral("ms"));
    const QCommandLineOption delayOption(QStringLiteral("delay"), QStringLiteral("Delay every reply by this many ms (fault injection)."), QStringLiteral("ms"));
    const QCommandLineOption dropOption(QStringLiteral("drop"), QStringLiteral("Percent chance (0..100) to drop the client instead of replying."), QStringLiteral("percent"));
    const QCommandLineOption verboseOption({QStringLiteral("v"), QStringLiteral("verbose")}, QStringLiteral("Log every request to stdout."));
    parser.addOptions({helpOption, versionOption, configOption, hostOption, portOption, unitOption, tickOption,
                       delayOption, dropOption, verboseOption});

    if (!parser.parse(QCoreApplication::arguments())) {
        return usageError(parser.errorText());
    }
    if (parser.isSet(versionOption)) {
        std::printf("modbus_test_server %s (libmodbus %s)\n", qPrintable(QCoreApplication::applicationVersion()),
                    LIBMODBUS_VERSION_STRING);
        return 0;
    }
    if (parser.isSet(helpOption)) {
        std::printf("%s", qPrintable(parser.helpText()));
        return 0;
    }
    if (!parser.positionalArguments().isEmpty()) {
        return usageError(QStringLiteral("unexpected argument '%1'").arg(parser.positionalArguments().first()));
    }

    mbsim::ServerConfig config = mbsim::ServerConfig::defaults();
    if (parser.isSet(configOption)) {
        QString error;
        const auto loaded = mbsim::loadServerConfig(parser.value(configOption), &error);
        if (!loaded) {
            return usageError(QStringLiteral("invalid config: %1").arg(error));
        }
        config = *loaded;
    }

    bool ok = true;
    if (parser.isSet(hostOption)) {
        config.host = parser.value(hostOption);
    }
    if (parser.isSet(portOption)) {
        const int port = parser.value(portOption).toInt(&ok);
        if (!ok || port < 0 || port > 65535) {
            return usageError(QStringLiteral("--port must be 0..65535"));
        }
        config.port = static_cast<quint16>(port);
    }
    if (parser.isSet(unitOption)) {
        config.unitId = parser.value(unitOption).toInt(&ok);
        if (!ok || config.unitId < 0 || config.unitId > 247) {
            return usageError(QStringLiteral("--unit must be 0..247"));
        }
    }
    if (parser.isSet(tickOption)) {
        config.tickMs = parser.value(tickOption).toInt(&ok);
        if (!ok || config.tickMs < 1 || config.tickMs > 60000) {
            return usageError(QStringLiteral("--tick must be 1..60000 ms"));
        }
    }
    if (parser.isSet(delayOption)) {
        config.delayMs = parser.value(delayOption).toInt(&ok);
        if (!ok || config.delayMs < 0 || config.delayMs > 600000) {
            return usageError(QStringLiteral("--delay must be 0..600000 ms"));
        }
    }
    if (parser.isSet(dropOption)) {
        config.dropPercent = parser.value(dropOption).toDouble(&ok);
        if (!ok || config.dropPercent < 0 || config.dropPercent > 100) {
            return usageError(QStringLiteral("--drop must be 0..100"));
        }
    }
    if (parser.isSet(verboseOption)) {
        config.verbose = true;
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
#endif

    mbsim::ServerLoop server(config);
    QString error;
    if (!server.start(&error)) {
        std::fprintf(stderr, "modbus_test_server: %s\n", qPrintable(error));
        return 1;
    }

    std::printf("modbus_test_server %s, libmodbus %s\n", qPrintable(QCoreApplication::applicationVersion()),
                LIBMODBUS_VERSION_STRING);
    std::printf("unit %d, holding registers 0..%d (read/write), input registers 0..%d, tick %d ms\n", config.unitId,
                config.holdingCount - 1, config.inputCount - 1, config.tickMs);
    for (const mbsim::SignalBinding &binding : std::as_const(config.signalBindings)) {
        const int registers = mbsim::registerCount(binding.format);
        if (registers == 2) {
            std::printf("  IR%u-%u: ", binding.inputAddress, binding.inputAddress + 1);
        } else {
            std::printf("  IR%u: ", binding.inputAddress);
        }
        std::printf("%s", kindName(binding.params.kind));
        if (binding.format != mbsim::RegisterFormat::Int16) {
            std::printf(" (%s %s)", formatName(binding.format), orderName(binding.byteOrder));
        }
        if (binding.params.kind == mbsim::SignalKind::Lag) {
            std::printf(" following HR%u (tau %g s)", binding.params.sourceAddress, binding.params.tauS);
        }
        std::printf("\n");
    }
    if (config.delayMs > 0 || config.dropPercent > 0) {
        std::printf("fault injection: delay %d ms, drop %g%%\n", config.delayMs, config.dropPercent);
    }
    std::printf("LISTENING %u\n", static_cast<unsigned>(server.boundPort()));
    std::fflush(stdout);

    return server.run(g_stop);
}
