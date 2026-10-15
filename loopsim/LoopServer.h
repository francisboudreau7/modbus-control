#pragma once

#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

struct _modbus;
struct _modbus_mapping_t;

namespace mbloop {

/// LIC-101 register values shared between the Modbus thread and the model's thread.
struct RegulatorRegisters {
    float pv = 0.0f;  ///< written by the model
    float sp = 50.0f; ///< written by Modbus clients or the faceplate
    float out = 0.0f; ///< written by Modbus clients or the faceplate
};

/// LIC-101 as a Modbus TCP server on its own thread (libmodbus, one select() loop for the
/// listening socket and all clients, timeout 50 ms). Any unit id is answered. float32 values
/// in ABCD order (high word first). Register map (protocol addresses; the documentation shows
/// them 1-based like the ABB CM50 manual Rev. I):
/// - input registers 0-1 (FC04): PV;
/// - holding registers 0-1 (FC03 read, FC16 write): SP; holding registers 2-3: OUT.
/// Rules, checked before libmodbus answers (replies built with modbus_reply_exception):
/// - FC04 must read exactly within [0, 1], starting at 0 (count 2); FC03 within [0, 3], starting
///   on a pair (0 or 2) with an even count (2 or 4); FC16 likewise for writes. Anything else,
///   including FC06, FC01/02/05/15/23 and other addresses: exception 2 (illegal data address).
/// - FC16 values must be finite and within 0..100 (both SP and OUT): otherwise exception 3
///   (illegal data value), nothing written.
/// - A successful SP/OUT write updates the shared registers immediately (the model applies OUT on
///   its next tick through takeWrites()).
/// Reads return the shared values at the time of the request.
class LoopServer
{
public:
    LoopServer();
    ~LoopServer(); ///< stops the thread and closes the sockets
    LoopServer(const LoopServer &) = delete;
    LoopServer &operator=(const LoopServer &) = delete;

    /// Listens on host:port (port 0 = any free port) and starts the server thread. False + error
    /// (e.g. "Port 1601 is already in use.") on failure.
    bool start(const QString &host, quint16 port, QString *error = nullptr);
    void stop();
    /// The port actually listened on; 0 when not started.
    quint16 port() const;
    /// Number of connected clients.
    int clientCount() const;

    /// Model side: publishes PV (and, after a faceplate change, SP/OUT) for the next reads.
    void publish(float pv, float sp, float out);
    /// Model side: SP and OUT written by clients since the last call (each nullopt-like flag
    /// false when not written), then clears them.
    struct Writes {
        bool spWritten = false;
        float sp = 0.0f;
        bool outWritten = false;
        float out = 0.0f;
    };
    Writes takeWrites();

private:
    // Implementation-defined; T212 may change the private section freely.
    void run();
    mutable std::mutex m_mutex;
    RegulatorRegisters m_regs;
    Writes m_writes;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_clients{0};
    std::thread m_thread;
    _modbus *m_ctx = nullptr;
    _modbus_mapping_t *m_mapping = nullptr;
    int m_listenSocket = -1;
    quint16 m_port = 0;
};

} // namespace mbloop
