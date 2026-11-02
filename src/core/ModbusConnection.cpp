#include "ModbusConnection.h"

#include <QCoreApplication>
#include <QStringList>

#include <modbus.h>

#include <cerrno>
#include <cstdint>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#endif

namespace mb {

namespace {

/// modbus_strerror() text; the libmodbus-specific codes are translated, the rest (C runtime
/// strerror) is shown as the system gives it.
QString libmodbusText(int err)
{
    switch (err) {
    case EMBXILFUN: return QCoreApplication::translate("mb::ModbusConnection", "Illegal function");
    case EMBXILADD: return QCoreApplication::translate("mb::ModbusConnection", "Illegal data address");
    case EMBXILVAL: return QCoreApplication::translate("mb::ModbusConnection", "Illegal data value");
    case EMBXSFAIL: return QCoreApplication::translate("mb::ModbusConnection", "Slave device or server failure");
    case EMBXACK: return QCoreApplication::translate("mb::ModbusConnection", "Acknowledge");
    case EMBXSBUSY: return QCoreApplication::translate("mb::ModbusConnection", "Slave device or server is busy");
    case EMBXNACK: return QCoreApplication::translate("mb::ModbusConnection", "Negative acknowledge");
    case EMBXMEMPAR: return QCoreApplication::translate("mb::ModbusConnection", "Memory parity error");
    case EMBXGPATH: return QCoreApplication::translate("mb::ModbusConnection", "Gateway path unavailable");
    case EMBXGTAR: return QCoreApplication::translate("mb::ModbusConnection", "Target device failed to respond");
    case EMBBADCRC: return QCoreApplication::translate("mb::ModbusConnection", "Invalid CRC");
    case EMBBADDATA: return QCoreApplication::translate("mb::ModbusConnection", "Invalid data");
    case EMBBADEXC: return QCoreApplication::translate("mb::ModbusConnection", "Invalid exception code");
    case EMBMDATA: return QCoreApplication::translate("mb::ModbusConnection", "Too many data");
    case EMBBADSLAVE: return QCoreApplication::translate("mb::ModbusConnection", "Response not from requested slave");
    default: return QString::fromLocal8Bit(modbus_strerror(err));
    }
}

ModbusResult makeResult(ModbusError error, const QString &message, int exceptionCode = 0)
{
    ModbusResult r;
    r.error = error;
    r.exceptionCode = exceptionCode;
    r.message = message;
    return r;
}

ModbusResult notConnected()
{
    return makeResult(ModbusError::NotConnected, QCoreApplication::translate("mb::ModbusConnection", "Not connected"));
}

ModbusResult invalidArgument(const QString &what)
{
    return makeResult(ModbusError::InvalidArgument, what);
}

/// libmodbus 3.2.0 on Windows does NOT translate Winsock failures (send/recv/select errors) to
/// errno: errno stays stale and only WSAGetLastError() is meaningful. So both are cleared before
/// each libmodbus call and both are captured immediately after a failure.
void clearErrors()
{
    errno = 0;
#ifdef _WIN32
    WSASetLastError(0);
#endif
}

struct LastError
{
    int err;
    int wsa;
};

LastError captureError()
{
    LastError e{errno, 0};
#ifdef _WIN32
    e.wsa = WSAGetLastError();
#endif
    return e;
}

/// Maps a libmodbus failure (captured right after the failing call) to a ModbusResult.
ModbusResult mapError(LastError last)
{
    int err = last.err;
    const int wsa = last.wsa;
#ifdef _WIN32
    // errno untouched by libmodbus: the Winsock error is the truth.
    if (err == 0 && wsa != 0) {
        const QString wtext = QString::fromLocal8Bit(
            std::system_category().message(wsa).c_str());
        switch (wsa) {
        case WSAETIMEDOUT:
            return makeResult(ModbusError::Timeout, wtext);
        case WSAECONNRESET: case WSAECONNABORTED: case WSAENOTCONN: case WSAENOTSOCK:
        case WSAESHUTDOWN: case WSAENETRESET: case WSAENETDOWN: case WSAECONNREFUSED:
        case WSAENETUNREACH: case WSAEHOSTUNREACH:
            return makeResult(ModbusError::ConnectionLost, wtext);
        default:
            return makeResult(ModbusError::Other, wtext);
        }
    }
#else
    Q_UNUSED(wsa)
#endif
    const QString text = libmodbusText(err);
    if (err >= EMBXILFUN && err <= EMBXGTAR) {
        const int code = err - MODBUS_ENOBASE;
        return makeResult(ModbusError::Exception,
                          
                          //: %1 = Modbus exception code number, %2 = its description
                          QCoreApplication::translate("mb::ModbusConnection", "Modbus exception %1: %2").arg(code).arg(text), code);
    }

    if (err == ETIMEDOUT)
        return makeResult(ModbusError::Timeout, text);
    if (err == ECONNRESET || err == ECONNABORTED || err == EPIPE || err == EBADF
        || err == ENOTCONN || err == ECONNREFUSED) {
        return makeResult(ModbusError::ConnectionLost, text);
    }
#ifdef _WIN32
    if (err == WSAETIMEDOUT)
        return makeResult(ModbusError::Timeout, text);
    if (err == WSAECONNRESET || err == WSAECONNABORTED || err == WSAENOTCONN
        || err == WSAENOTSOCK || err == WSAESHUTDOWN || err == WSAENETRESET
        || err == WSAENETDOWN || err == WSAECONNREFUSED) {
        return makeResult(ModbusError::ConnectionLost, text);
    }
#endif
    return makeResult(ModbusError::Other, text);
}

/// Parses a dotted-quad IPv4 literal; returns false if invalid.
bool isIPv4Literal(const QString &s)
{
    const QStringList parts = s.split(QLatin1Char('.'));
    if (parts.size() != 4)
        return false;
    for (const QString &p : parts) {
        if (p.isEmpty() || p.size() > 3)
            return false;
        for (const QChar c : p) {
            if (c < QLatin1Char('0') || c > QLatin1Char('9'))
                return false;
        }
        if (p.toInt() > 255)
            return false;
    }
    return true;
}

bool rangeOk(quint16 address, int count)
{
    return count >= 1 && static_cast<int>(address) + count <= 65536;
}

} // namespace

ModbusConnection::~ModbusConnection()
{
    close();
}

void ModbusConnection::close()
{
    if (m_ctx) {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }
}

void ModbusConnection::flush()
{
    if (m_ctx)
        modbus_flush(m_ctx);
}

ModbusResult ModbusConnection::open(const ConnectionSettings &settings)
{
    close();

    const QString host = settings.host.trimmed();
    if (host.isEmpty())
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Host must not be empty"));
    if (settings.port < 1) // quint16: 0 is the only invalid value
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Port must be 1..65535"));
    if (!((settings.unitId >= 0 && settings.unitId <= 247) || settings.unitId == 255))
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Unit id must be 0..247 or 255"));
    if (settings.timeoutMs < 1)
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Timeout must be at least 1 ms"));

    // libmodbus only accepts IPv4 literals (inet_pton); name resolution would need QtNetwork.
    QString ip = host;
    if (ip.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        ip = QStringLiteral("127.0.0.1");
    if (!isIPv4Literal(ip)) {
        return invalidArgument(
            QCoreApplication::translate("mb::ModbusConnection", "Host must be an IPv4 address (e.g. 192.168.1.10) or 'localhost'"));
    }

    const QByteArray ipBytes = ip.toLatin1();
    modbus_t *ctx = modbus_new_tcp(ipBytes.constData(), settings.port);
    if (!ctx) {
        return makeResult(ModbusError::ConnectFailed,
                          libmodbusText(errno));
    }

    const uint32_t sec = static_cast<uint32_t>(settings.timeoutMs / 1000);
    const uint32_t usec = static_cast<uint32_t>(settings.timeoutMs % 1000) * 1000u;
    const bool configured = modbus_set_slave(ctx, settings.unitId) == 0
        && modbus_set_response_timeout(ctx, sec, usec) == 0
        && modbus_set_byte_timeout(ctx, sec, usec) == 0
        && modbus_set_error_recovery(ctx, MODBUS_ERROR_RECOVERY_NONE) == 0;
    if (!configured) {
        const int err = errno;
        modbus_free(ctx);
        return makeResult(ModbusError::InvalidArgument,
                          libmodbusText(err));
    }

    clearErrors();
    if (modbus_connect(ctx) == -1) {
        const LastError last = captureError();
        QString text = mapError(last).message;
        modbus_free(ctx);
        return makeResult(ModbusError::ConnectFailed, text);
    }

    m_ctx = ctx;
    return {};
}

ModbusResult ModbusConnection::readRegisters(Area area, quint16 address, quint16 count,
                                             QList<quint16> &out)
{
    if (!m_ctx)
        return notConnected();
    if (isBitArea(area) || count == 0 || count > MODBUS_MAX_READ_REGISTERS || !rangeOk(address, count))
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Invalid register count or address range"));

    QList<quint16> buf(count);
    clearErrors();
    const int rc = area == Area::InputRegister
        ? modbus_read_input_registers(m_ctx, address, count, buf.data())
        : modbus_read_registers(m_ctx, address, count, buf.data());
    if (rc == -1)
        return mapError(captureError());
    if (rc == 0 || rc != count) {
        // Closed socket / short read: treat as lost connection.
        return makeResult(ModbusError::ConnectionLost, QCoreApplication::translate("mb::ModbusConnection", "Connection closed"));
    }

    out = std::move(buf);
    return {};
}

ModbusResult ModbusConnection::readBits(Area area, quint16 address, quint16 count, QList<quint16> &out)
{
    if (!m_ctx)
        return notConnected();
    if (!isBitArea(area) || count == 0 || count > MODBUS_MAX_READ_BITS || !rangeOk(address, count))
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Invalid bit count or address range"));

    std::vector<uint8_t> bits(count);
    clearErrors();
    const int rc = area == Area::DiscreteInput
        ? modbus_read_input_bits(m_ctx, address, count, bits.data())
        : modbus_read_bits(m_ctx, address, count, bits.data());
    if (rc == -1)
        return mapError(captureError());
    if (rc == 0 || rc != count)
        return makeResult(ModbusError::ConnectionLost, QCoreApplication::translate("mb::ModbusConnection", "Connection closed"));

    QList<quint16> buf;
    buf.reserve(count);
    for (const uint8_t b : bits)
        buf.append(b ? 1 : 0);
    out = std::move(buf);
    return {};
}

ModbusResult ModbusConnection::writeCoil(quint16 address, bool on)
{
    if (!m_ctx)
        return notConnected();
    clearErrors();
    if (modbus_write_bit(m_ctx, address, on ? 1 : 0) == -1)
        return mapError(captureError());
    return {};
}

ModbusResult ModbusConnection::writeCoils(quint16 address, const QList<quint16> &values)
{
    if (!m_ctx)
        return notConnected();
    const int n = static_cast<int>(values.size());
    if (n < 1 || n > MODBUS_MAX_WRITE_BITS || !rangeOk(address, n))
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Invalid coil count or address range"));
    std::vector<uint8_t> bits;
    bits.reserve(values.size());
    for (const quint16 v : values)
        bits.push_back(v != 0 ? 1 : 0);
    clearErrors();
    if (modbus_write_bits(m_ctx, address, n, bits.data()) == -1)
        return mapError(captureError());
    return {};
}

ModbusResult ModbusConnection::writeSingle(quint16 address, quint16 value)
{
    if (!m_ctx)
        return notConnected();
    clearErrors();
    if (modbus_write_register(m_ctx, address, value) == -1)
        return mapError(captureError());
    return {};
}

ModbusResult ModbusConnection::writeMultiple(quint16 address, const QList<quint16> &values)
{
    if (!m_ctx)
        return notConnected();
    const int n = static_cast<int>(values.size());
    if (n < 1 || n > MODBUS_MAX_WRITE_REGISTERS || !rangeOk(address, n))
        return invalidArgument(QCoreApplication::translate("mb::ModbusConnection", "Invalid register count or address range"));
    clearErrors();
    if (modbus_write_registers(m_ctx, address, n, values.constData()) == -1)
        return mapError(captureError());
    return {};
}

} // namespace mb
