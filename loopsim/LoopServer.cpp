#include "LoopServer.h"

// Stubs created by T210; implemented by T212.

namespace mbloop {

LoopServer::LoopServer() = default;
LoopServer::~LoopServer() { stop(); }

bool LoopServer::start(const QString &, quint16, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented yet.");
    return false;
}

void LoopServer::stop() {}
quint16 LoopServer::port() const { return m_port; }
int LoopServer::clientCount() const { return m_clients; }

void LoopServer::publish(float pv, float sp, float out)
{
    std::lock_guard lock(m_mutex);
    m_regs = {pv, sp, out};
}

LoopServer::Writes LoopServer::takeWrites()
{
    std::lock_guard lock(m_mutex);
    const Writes w = m_writes;
    m_writes = {};
    return w;
}

void LoopServer::run() {}

} // namespace mbloop
