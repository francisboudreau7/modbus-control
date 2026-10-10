#pragma once

#include "SignalGen.h"

#include <QList>
#include <QString>

#include <optional>

namespace mbsim {

/// Drives input register `inputAddress` with a simulated signal.
struct SignalBinding {
    quint16 inputAddress = 0; ///< first register; 2-register formats also use inputAddress + 1
    SignalParams params;
    RegisterFormat format = RegisterFormat::Int16;
    WordOrder byteOrder = WordOrder::ABCD; ///< 2-register formats only
};

/// Test server configuration. Registers are mapped from address 0.
///
/// server.json (every key optional; missing keys keep the defaults below):
/// {
///   "host": "127.0.0.1", "port": 1502, "unitId": 1,
///   "holdingCount": 200, "inputCount": 64, "tickMs": 50,
///   "delayMs": 0, "dropPercent": 0, "verbose": false, "logWrites": false,
///   "signals": [ { "input": 0, "kind": "sine"|"triangle"|"sawtooth"|"square"|"counter"|
///                  "noise"|"lag"|"constant", "amplitude": 1000, "offset": 0,
///                  "periodS": 5, "tauS": 2, "deadTimeS": 0, "source": 100,
///                  "format": "int16"|"int32"|"float32", "byteOrder": "ABCD"|"CDAB"|"BADC"|"DCBA" },
///                ... ]
/// }
/// "host" defaults to loopback because the server has no authentication; use "0.0.0.0" to
/// accept remote clients.
/// If "signals" is present it replaces the default bindings entirely.
struct ServerConfig {
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 1502;
    int unitId = 1;
    int holdingCount = 200; ///< HR 0..holdingCount-1, all read/write, initially 0
    int inputCount = 64;    ///< IR 0..inputCount-1, unbound ones stay 0
    int tickMs = 50;        ///< simulation update period
    int delayMs = 0;        ///< fault injection: delay before every reply
    double dropPercent = 0; ///< fault injection: chance (0..100) to drop a client per request
    bool verbose = false;   ///< log each request to stdout
    bool logWrites = false; ///< log only write requests (hex registers, 32-bit decodings)
    QList<SignalBinding> signalBindings;

    /// Defaults: IR0 sine(1000, 5 s), IR1 triangle(1000, 10 s), IR2 counter,
    /// IR3 square(1000, 4 s), IR4 noise(100), IR5 sawtooth(1000, 8 s),
    /// IR10 lag following HR100 (tau 2 s, dead time 1 s),
    /// IR20-21 float32 ABCD sine(123.456, 5 s), IR22-23 float32 CDAB sine(123.456, 5 s),
    /// IR24-25 int32 ABCD sawtooth(100000, 8 s).
    static ServerConfig defaults();
};

/// Parses server.json content on top of ServerConfig::defaults(). Fails on invalid
/// JSON, wrong value types, unknown signal kind, out-of-range values (port 0, counts
/// outside 1..65536, binding address + registerCount(format) > inputCount, Lag source >=
/// holdingCount, unknown format/byteOrder strings,
/// periodS/tauS <= 0, deadTimeS outside 0..60, tickMs < 1, dropPercent outside 0..100).
std::optional<ServerConfig> parseServerConfig(const QByteArray &json, QString *error = nullptr);
std::optional<ServerConfig> loadServerConfig(const QString &path, QString *error = nullptr);

} // namespace mbsim
