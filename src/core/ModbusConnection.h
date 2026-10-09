#pragma once

#include "Tag.h"

#include <QList>
#include <QMetaType>
#include <QString>

struct _modbus; // libmodbus context (modbus_t), kept out of this header

namespace mb {

/// Connection parameters; also persisted in the project file.
struct ConnectionSettings {
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 502;
    int unitId = 1;          ///< 0..247 (255 accepted for TCP)
    int timeoutMs = 1000;    ///< response timeout
    int pollPeriodMs = 500;  ///< >= 100

    friend bool operator==(const ConnectionSettings &, const ConnectionSettings &) = default;
};

enum class ModbusError : quint8 {
    None,
    NotConnected,    ///< operation attempted while not open
    ConnectFailed,   ///< TCP connect failed
    Timeout,         ///< no response within timeoutMs
    Exception,       ///< Modbus exception response; see ModbusResult::exceptionCode
    ConnectionLost,  ///< socket error / reset; caller should close and reconnect
    InvalidArgument, ///< e.g. count 0 or > protocol limit
    Other,
};

struct ModbusResult {
    ModbusError error = ModbusError::None;
    int exceptionCode = 0; ///< 1..11 when error == Exception (e.g. 2 = illegal data address)
    QString message;       ///< human-readable, from modbus_strerror where applicable

    bool ok() const { return error == ModbusError::None; }
};

/// Thin RAII wrapper around one libmodbus TCP context. Not thread-safe: use from one
/// thread only (PollWorker's thread). Protocol limits: read count 1..125, write
/// multiple count 1..123.
class ModbusConnection
{
public:
    ModbusConnection() = default;
    ~ModbusConnection();
    ModbusConnection(const ModbusConnection &) = delete;
    ModbusConnection &operator=(const ModbusConnection &) = delete;

    /// Closes any open context, then creates, configures (unit id, timeout) and connects.
    ModbusResult open(const ConnectionSettings &settings);
    void close();
    bool isOpen() const { return m_ctx != nullptr; }

    /// FC03 for HoldingRegister, FC04 for InputRegister. On success `out` holds `count` values.
    ModbusResult readRegisters(Area area, quint16 address, quint16 count, QList<quint16> &out);
    /// FC06.
    ModbusResult writeSingle(quint16 address, quint16 value);
    /// FC16.
    ModbusResult writeMultiple(quint16 address, const QList<quint16> &values);

private:
    _modbus *m_ctx = nullptr;
};

} // namespace mb

Q_DECLARE_METATYPE(mb::ConnectionSettings)
Q_DECLARE_METATYPE(mb::ModbusResult)
