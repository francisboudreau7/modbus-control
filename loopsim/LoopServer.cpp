#include "LoopServer.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <modbus.h>

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace mbloop {

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
#else
using NativeSocket = int;
#endif

constexpr int kMaxClients = 60;  // select() on Windows is limited to FD_SETSIZE (64) sockets
constexpr int kHoldingCount = 4; // SP (0-1), OUT (2-3)
constexpr int kInputCount = 2;   // PV (0-1)

NativeSocket native(int fd)
{
    return static_cast<NativeSocket>(fd);
}

void closeSocket(int fd)
{
#ifdef _WIN32
    ::closesocket(native(fd));
#else
    ::close(fd);
#endif
}

unsigned be16(const unsigned char *p)
{
    return (unsigned(p[0]) << 8) | unsigned(p[1]);
}

/// float32 ABCD: high word first.
void encodeFloat(float value, quint16 *regs)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    regs[0] = static_cast<quint16>(bits >> 16);
    regs[1] = static_cast<quint16>(bits & 0xFFFFu);
}

float decodeFloat(const unsigned char *bytes)
{
    const std::uint32_t bits = (std::uint32_t(bytes[0]) << 24) | (std::uint32_t(bytes[1]) << 16)
        | (std::uint32_t(bytes[2]) << 8) | std::uint32_t(bytes[3]);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool validPair(unsigned address, unsigned count, unsigned registers)
{
    return (address == 0 || address == 2) && (count == 2 || count == 4) && address + count <= registers;
}

/// Result of checking one request before libmodbus answers it.
struct Verdict {
    int exception = 0; ///< 0 = ok, else a Modbus exception code to send
    bool spWrite = false;
    float sp = 0.0f;
    bool outWrite = false;
    float out = 0.0f;
};

/// `pdu` points after the function code, `pduLen` bytes.
Verdict checkRequest(unsigned fc, const unsigned char *pdu, int pduLen)
{
    Verdict v;
    switch (fc) {
    case MODBUS_FC_READ_INPUT_REGISTERS:
        if (pduLen < 4) {
            v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        } else if (be16(pdu) != 0 || be16(pdu + 2) != 2) {
            v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }
        break;
    case MODBUS_FC_READ_HOLDING_REGISTERS:
        if (pduLen < 4) {
            v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
        } else if (!validPair(be16(pdu), be16(pdu + 2), kHoldingCount)) {
            v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        }
        break;
    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS: {
        if (pduLen < 4) {
            v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
            break;
        }
        const unsigned address = be16(pdu);
        const unsigned count = be16(pdu + 2);
        if (!validPair(address, count, kHoldingCount)) {
            v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
            break;
        }
        if (pduLen < 5 || unsigned(pdu[4]) != count * 2 || pduLen < 5 + int(count) * 2) {
            v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
            break;
        }
        const unsigned char *data = pdu + 5;
        for (unsigned i = 0; i < count / 2; ++i) {
            const float value = decodeFloat(data + i * 4);
            if (!std::isfinite(value) || value < 0.0f || value > 100.0f) {
                v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE;
                return v;
            }
            if (address + i * 2 == 0) {
                v.spWrite = true;
                v.sp = value;
            } else {
                v.outWrite = true;
                v.out = value;
            }
        }
        break;
    }
    default:
        v.exception = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
        break;
    }
    return v;
}

} // namespace

LoopServer::LoopServer() = default;

LoopServer::~LoopServer()
{
    stop();
}

bool LoopServer::start(const QString &host, quint16 port, QString *error)
{
    auto fail = [this, error](const QString &message) {
        if (error) {
            *error = message;
        }
        if (m_listenSocket != -1) {
            closeSocket(m_listenSocket);
            m_listenSocket = -1;
        }
        if (m_ctx) {
            modbus_free(m_ctx);
            m_ctx = nullptr;
        }
        if (m_mapping) {
            modbus_mapping_free(m_mapping);
            m_mapping = nullptr;
        }
        m_port = 0;
        return false;
    };

    if (m_thread.joinable() || m_ctx) {
        if (error) {
            *error = QStringLiteral("The Modbus server is already running.");
        }
        return false;
    }

    const QByteArray hostBytes = host.toLatin1();
    const bool anyHost = hostBytes.isEmpty() || hostBytes == "0.0.0.0";
    m_ctx = modbus_new_tcp(anyHost ? nullptr : hostBytes.constData(), port);
    if (!m_ctx) {
        return fail(QStringLiteral("Cannot create the Modbus server on %1:%2: %3")
                        .arg(host)
                        .arg(port)
                        .arg(QString::fromLocal8Bit(modbus_strerror(errno))));
    }
    m_mapping = modbus_mapping_new_start_address(0, 0, 0, 0, 0, kHoldingCount, 0, kInputCount);
    if (!m_mapping) {
        return fail(QStringLiteral("Cannot allocate the register mapping."));
    }

#ifdef _WIN32
    // libmodbus sets SO_REUSEADDR, which on Windows lets a second process bind the same port
    // silently. Probe with an exclusive bind first so "port in use" is reported.
    if (port != 0) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            const SOCKET probe = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (probe != INVALID_SOCKET) {
                const BOOL exclusive = TRUE;
                ::setsockopt(probe, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
                             sizeof(exclusive));
                sockaddr_in probeAddr {};
                probeAddr.sin_family = AF_INET;
                probeAddr.sin_port = htons(port);
                bool addrOk = true;
                if (anyHost) {
                    probeAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                } else {
                    addrOk = ::inet_pton(AF_INET, hostBytes.constData(), &probeAddr.sin_addr) == 1;
                }
                const bool inUse = addrOk
                    && ::bind(probe, reinterpret_cast<sockaddr *>(&probeAddr), sizeof(probeAddr)) == SOCKET_ERROR
                    && WSAGetLastError() == WSAEADDRINUSE;
                ::closesocket(probe);
                if (inUse) {
                    WSACleanup();
                    return fail(QStringLiteral("Port %1 is already in use.").arg(port));
                }
            }
            WSACleanup();
        }
    }
#endif

    m_listenSocket = modbus_tcp_listen(m_ctx, 16);
    if (m_listenSocket == -1) {
        return fail(QStringLiteral("Cannot listen on %1:%2 (port in use or invalid address?): %3")
                        .arg(host)
                        .arg(port)
                        .arg(QString::fromLocal8Bit(modbus_strerror(errno))));
    }

    sockaddr_in addr {};
    socklen_t len = sizeof(addr);
    if (::getsockname(native(m_listenSocket), reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        return fail(QStringLiteral("getsockname failed."));
    }
    m_port = ntohs(addr.sin_port);

    m_stop = false;
    m_clients = 0;
    m_thread = std::thread([this] { run(); });
    return true;
}

void LoopServer::stop()
{
    m_stop = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_listenSocket != -1) {
        closeSocket(m_listenSocket);
        m_listenSocket = -1;
    }
    if (m_ctx) {
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }
    if (m_mapping) {
        modbus_mapping_free(m_mapping);
        m_mapping = nullptr;
    }
    m_clients = 0;
    m_port = 0;
}

quint16 LoopServer::port() const
{
    return m_port;
}

int LoopServer::clientCount() const
{
    return m_clients;
}

void LoopServer::publish(float pv, float sp, float out)
{
    std::lock_guard lock(m_mutex);
    m_regs.pv = pv;
    // A client write not yet taken by the model wins over the model's older value.
    if (!m_writes.spWritten) {
        m_regs.sp = sp;
    }
    if (!m_writes.outWritten) {
        m_regs.out = out;
    }
}

LoopServer::Writes LoopServer::takeWrites()
{
    std::lock_guard lock(m_mutex);
    const Writes w = m_writes;
    m_writes = {};
    return w;
}

void LoopServer::run()
{
    std::vector<unsigned char> query(MODBUS_TCP_MAX_ADU_LENGTH);
    std::vector<int> clients;

    auto closeClient = [&](int fd) {
        closeSocket(fd);
        clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
        m_clients = static_cast<int>(clients.size());
    };

    while (!m_stop.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(native(m_listenSocket), &readSet);
        int maxFd = m_listenSocket;
        for (const int fd : clients) {
            FD_SET(native(fd), &readSet);
            maxFd = std::max(maxFd, fd);
        }
        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 50 * 1000;

        const int ready = ::select(maxFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue; // timeout or interrupted: look at m_stop
        }

        if (FD_ISSET(native(m_listenSocket), &readSet)) {
            int listenSocket = m_listenSocket;
            const int fd = modbus_tcp_accept(m_ctx, &listenSocket);
            if (fd >= 0) {
                if (static_cast<int>(clients.size()) >= kMaxClients) {
                    closeSocket(fd);
                } else {
                    clients.push_back(fd);
                    m_clients = static_cast<int>(clients.size());
                }
            }
        }

        const std::vector<int> snapshot = clients; // closeClient mutates the list
        for (const int fd : snapshot) {
            if (!FD_ISSET(native(fd), &readSet)) {
                continue;
            }
            modbus_set_socket(m_ctx, fd);
            const int rc = modbus_receive(m_ctx, query.data());
            if (rc == -1) {
                closeClient(fd);
                continue;
            }
            const int hl = modbus_get_header_length(m_ctx);
            if (rc <= hl) {
                continue;
            }
            const Verdict verdict = checkRequest(query[static_cast<size_t>(hl)], query.data() + hl + 1, rc - hl - 1);
            if (verdict.exception != 0) {
                if (modbus_reply_exception(m_ctx, query.data(), static_cast<unsigned>(verdict.exception)) == -1) {
                    closeClient(fd);
                }
                continue;
            }

            {
                // Bring the mapping up to date with the shared registers before libmodbus reads it.
                std::lock_guard lock(m_mutex);
                encodeFloat(m_regs.pv, m_mapping->tab_input_registers);
                encodeFloat(m_regs.sp, m_mapping->tab_registers);
                encodeFloat(m_regs.out, m_mapping->tab_registers + 2);
            }
            if (modbus_reply(m_ctx, query.data(), rc, m_mapping) == -1) {
                closeClient(fd);
                continue;
            }
            if (verdict.spWrite || verdict.outWrite) {
                std::lock_guard lock(m_mutex);
                if (verdict.spWrite) {
                    m_regs.sp = verdict.sp;
                    m_writes.spWritten = true;
                    m_writes.sp = verdict.sp;
                }
                if (verdict.outWrite) {
                    m_regs.out = verdict.out;
                    m_writes.outWritten = true;
                    m_writes.out = verdict.out;
                }
            }
        }
    }

    for (const int fd : clients) {
        closeSocket(fd);
    }
    m_clients = 0;
}

} // namespace mbloop
