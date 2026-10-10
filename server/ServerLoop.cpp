#include "ServerLoop.h"

#include "RequestLog.h"

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

#include <QTime>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

namespace mbsim {

namespace {

using Clock = std::chrono::steady_clock;

#ifdef _WIN32
using NativeSocket = SOCKET;
#else
using NativeSocket = int;
#endif

constexpr int kMaxClients = 60; // select() on Windows is limited to FD_SETSIZE (64) sockets

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

} // namespace

ServerLoop::ServerLoop(const ServerConfig &config)
    : m_config(config)
{
}

ServerLoop::~ServerLoop()
{
    shutdown();
}

quint16 ServerLoop::boundPort() const
{
    return m_boundPort;
}

void ServerLoop::shutdown()
{
    for (const int fd : std::as_const(m_clients)) {
        closeSocket(fd);
    }
    m_clients.clear();
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
    m_boundPort = 0;
}

bool ServerLoop::start(QString *error)
{
    auto fail = [this, error](const QString &message) {
        if (error) {
            *error = message;
        }
        shutdown();
        return false;
    };

    if (m_ctx) {
        return fail(QStringLiteral("server already started"));
    }

    // libmodbus calls WSAStartup itself (in modbus_tcp_listen / modbus_tcp_accept).
    const QByteArray host = m_config.host.toLatin1();
    const bool anyHost = host.isEmpty() || host == "0.0.0.0";
    m_ctx = modbus_new_tcp(anyHost ? nullptr : host.constData(), m_config.port);
    if (!m_ctx) {
        return fail(QStringLiteral("modbus_new_tcp failed: %1").arg(QString::fromLocal8Bit(modbus_strerror(errno))));
    }
    if (modbus_set_slave(m_ctx, m_config.unitId) == -1) {
        return fail(QStringLiteral("invalid unit id %1: %2")
                        .arg(m_config.unitId)
                        .arg(QString::fromLocal8Bit(modbus_strerror(errno))));
    }

    m_mapping = modbus_mapping_new_start_address(0, 0, 0, 0, 0, m_config.holdingCount, 0, m_config.inputCount);
    if (!m_mapping) {
        return fail(QStringLiteral("cannot allocate register mapping: %1")
                        .arg(QString::fromLocal8Bit(modbus_strerror(errno))));
    }

#ifdef _WIN32
    // libmodbus sets SO_REUSEADDR, which on Windows lets a second process bind the same
    // port silently. Probe with an exclusive bind first so "port in use" is reported.
    if (m_config.port != 0) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            const SOCKET probe = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (probe != INVALID_SOCKET) {
                const BOOL exclusive = TRUE;
                ::setsockopt(probe, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&exclusive),
                             sizeof(exclusive));
                sockaddr_in probeAddr {};
                probeAddr.sin_family = AF_INET;
                probeAddr.sin_port = htons(m_config.port);
                bool addrOk = true;
                if (anyHost) {
                    probeAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                } else {
                    addrOk = ::inet_pton(AF_INET, host.constData(), &probeAddr.sin_addr) == 1;
                }
                const bool inUse = addrOk
                    && ::bind(probe, reinterpret_cast<sockaddr *>(&probeAddr), sizeof(probeAddr)) == SOCKET_ERROR
                    && WSAGetLastError() == WSAEADDRINUSE;
                ::closesocket(probe);
                if (inUse) {
                    WSACleanup();
                    return fail(QStringLiteral("cannot listen on %1:%2: port already in use")
                                    .arg(m_config.host)
                                    .arg(m_config.port));
                }
            }
            WSACleanup();
        }
    }
#endif

    m_listenSocket = modbus_tcp_listen(m_ctx, 16);
    if (m_listenSocket == -1) {
        return fail(QStringLiteral("cannot listen on %1:%2 (port in use or invalid address?): %3")
                        .arg(m_config.host)
                        .arg(m_config.port)
                        .arg(QString::fromLocal8Bit(modbus_strerror(errno))));
    }

    sockaddr_in addr {};
    socklen_t len = sizeof(addr);
    if (::getsockname(native(m_listenSocket), reinterpret_cast<sockaddr *>(&addr), &len) != 0) {
        return fail(QStringLiteral("getsockname failed"));
    }
    m_boundPort = ntohs(addr.sin_port);

    m_generators.clear();
    quint32 seed = 1;
    for (const SignalBinding &binding : std::as_const(m_config.signalBindings)) {
        m_generators.append(SignalGenerator(binding.params, seed++));
    }
    return true;
}

void ServerLoop::tick(double t, double dt)
{
    for (qsizetype i = 0; i < m_generators.size(); ++i) {
        const SignalBinding &binding = m_config.signalBindings.at(i);
        quint16 input = 0;
        if (binding.params.kind == SignalKind::Lag) {
            input = m_mapping->tab_registers[binding.params.sourceAddress];
        }
        const double value = m_generators[i].nextValue(t, dt, input);

        quint16 regs[2] = {0, 0};
        if (binding.params.kind == SignalKind::Counter && binding.format == RegisterFormat::Int16) {
            // The counter is a raw uint16 that wraps at 65535; the int16 encoding would clamp it at 32767.
            regs[0] = static_cast<quint16>(value);
        } else {
            encodeRegisters(value, binding.format, binding.byteOrder, regs);
        }
        m_mapping->tab_input_registers[binding.inputAddress] = regs[0];
        if (registerCount(binding.format) == 2) {
            m_mapping->tab_input_registers[binding.inputAddress + 1] = regs[1];
        }
    }
}

void ServerLoop::closeClient(int fd, const char *why)
{
    closeSocket(fd);
    m_clients.removeOne(fd);
    if (m_config.verbose) {
        std::printf("[%d] disconnected (%s)\n", fd, why);
        std::fflush(stdout);
    }
}

void ServerLoop::logRequest(int fd, const unsigned char *query, int length)
{
    const int hl = modbus_get_header_length(m_ctx);
    if (length < hl + 1) {
        return;
    }
    const unsigned unit = query[hl - 1];
    const unsigned fc = query[hl];
    const unsigned char *pdu = query + hl + 1; // after function code
    const int pduLen = length - hl - 1;
    const QByteArray stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")).toLatin1();
    std::printf("%s %s\n", stamp.constData(), describeRequest(fd, unit, fc, pdu, pduLen).c_str());

    // FC06 mode writes a 32-bit value as two single writes: decode a consecutive pair together.
    if (fc == MODBUS_FC_WRITE_SINGLE_REGISTER && pduLen >= 4) {
        const unsigned addr = (unsigned(pdu[0]) << 8) | pdu[1];
        const quint16 value = quint16((unsigned(pdu[2]) << 8) | pdu[3]);
        const auto last = m_lastSingleWrite.constFind(fd);
        if (last != m_lastSingleWrite.constEnd() && addr == last->first + 1) {
            std::printf("%s [%d]   FC06 pair addr=%u-%u values=0x%04X,0x%04X | %s\n", stamp.constData(), fd,
                        last->first, addr, unsigned(last->second), unsigned(value),
                        describe32(last->second, value).c_str());
            m_lastSingleWrite.remove(fd);
        } else {
            m_lastSingleWrite.insert(fd, {addr, value});
        }
    } else if (isWriteFunction(fc)) {
        m_lastSingleWrite.remove(fd);
    }
    std::fflush(stdout);
}

int ServerLoop::run(const std::atomic<bool> &stop)
{
    if (!m_ctx || !m_mapping || m_listenSocket == -1) {
        return 1;
    }

    std::mt19937 rng(static_cast<std::mt19937::result_type>(Clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<double> uniform(0.0, 100.0);

    const auto tickPeriod = std::chrono::milliseconds(std::max(1, m_config.tickMs));
    const auto startTime = Clock::now();
    auto lastTick = startTime;
    tick(0.0, 0.0);
    auto nextTick = startTime + tickPeriod;

    std::vector<unsigned char> query(MODBUS_TCP_MAX_ADU_LENGTH);

    while (!stop.load()) {
        auto now = Clock::now();
        if (now >= nextTick) {
            const double t = std::chrono::duration<double>(now - startTime).count();
            const double dt = std::chrono::duration<double>(now - lastTick).count();
            tick(t, dt);
            lastTick = now;
            nextTick = now + tickPeriod;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(native(m_listenSocket), &readSet);
        int maxFd = m_listenSocket;
        for (const int fd : std::as_const(m_clients)) {
            FD_SET(native(fd), &readSet);
            maxFd = std::max(maxFd, fd);
        }

        now = Clock::now();
        auto wait = nextTick > now ? std::chrono::duration_cast<std::chrono::microseconds>(nextTick - now)
                                   : std::chrono::microseconds(0);
        wait = std::min<std::chrono::microseconds>(wait, tickPeriod);
        timeval timeout {};
        timeout.tv_sec = static_cast<long>(wait.count() / 1000000);
        timeout.tv_usec = static_cast<long>(wait.count() % 1000000);

        const int ready = ::select(maxFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue; // timeout (tick due) or interrupted
        }

        if (FD_ISSET(native(m_listenSocket), &readSet)) {
            int listenSocket = m_listenSocket;
            const int fd = modbus_tcp_accept(m_ctx, &listenSocket);
            if (fd >= 0) {
                if (m_clients.size() >= kMaxClients) {
                    closeSocket(fd);
                } else {
                    m_clients.append(fd);
                    if (m_config.verbose) {
                        std::printf("[%d] connected\n", fd);
                        std::fflush(stdout);
                    }
                }
            }
        }

        const QList<int> clients = m_clients; // copy: closeClient mutates the list
        for (const int fd : clients) {
            if (!FD_ISSET(native(fd), &readSet)) {
                continue;
            }
            modbus_set_socket(m_ctx, fd);
            const int rc = modbus_receive(m_ctx, query.data());
            if (rc == -1) {
                closeClient(fd, "closed or error");
                continue;
            }
            if (rc == 0) {
                continue; // request for another unit id
            }

            if (m_config.verbose
                || (m_config.logWrites && rc > modbus_get_header_length(m_ctx)
                    && isWriteFunction(query[modbus_get_header_length(m_ctx)]))) {
                logRequest(fd, query.data(), rc);
            }
            if (m_config.delayMs > 0) {
                const auto until = Clock::now() + std::chrono::milliseconds(m_config.delayMs);
                while (!stop.load()) {
                    const auto remaining = until - Clock::now();
                    if (remaining <= Clock::duration::zero()) {
                        break;
                    }
                    std::this_thread::sleep_for(std::min<Clock::duration>(std::chrono::milliseconds(20), remaining));
                }
            }
            if (m_config.dropPercent > 0 && uniform(rng) < m_config.dropPercent) {
                closeClient(fd, "fault injection: dropped");
                continue;
            }
            if (modbus_reply(m_ctx, query.data(), rc, m_mapping) == -1) {
                closeClient(fd, "reply failed");
            }
        }
    }

    shutdown();
    return 0;
}

} // namespace mbsim
