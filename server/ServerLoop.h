#pragma once

#include "ServerConfig.h"
#include "SignalGen.h"

#include <QList>
#include <QString>

#include <atomic>

struct _modbus;
struct _modbus_mapping_t;

namespace mbsim {

/// Single-threaded Modbus TCP server: one select() loop serves the listening socket
/// and all clients, and its timeout drives the simulation tick, so the register
/// mapping needs no locking.
class ServerLoop
{
public:
    explicit ServerLoop(const ServerConfig &config);
    ~ServerLoop();
    ServerLoop(const ServerLoop &) = delete;
    ServerLoop &operator=(const ServerLoop &) = delete;

    /// Allocates the mapping and starts listening. False + error on failure
    /// (e.g. port in use).
    bool start(QString *error = nullptr);
    /// Serves until `stop` becomes true (checked at least every tick). Returns the
    /// process exit code.
    int run(const std::atomic<bool> &stop);

private:
    // Implementation-defined; T12 may change the private section freely.
    ServerConfig m_config;
    _modbus *m_ctx = nullptr;
    _modbus_mapping_t *m_mapping = nullptr;
    int m_listenSocket = -1;
    QList<SignalGenerator> m_generators;
};

} // namespace mbsim
