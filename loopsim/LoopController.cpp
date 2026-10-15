#include "LoopController.h"

#include <QTimer>

// Stubs created by T210; implemented by T212.

namespace mbloop {

LoopController::LoopController(quint32 seed, QObject *parent) : QObject(parent), m_model(seed)
{
    m_model.reset(LoopParams{}, 50.0);
}

LoopController::~LoopController() = default;

bool LoopController::startServer(const QString &host, quint16 port, QString *error)
{
    return m_server.start(host, port, error);
}

LoopServer &LoopController::server() { return m_server; }
const LoopModel &LoopController::model() const { return m_model; }
double LoopController::speedFactor() const { return m_speed; }
void LoopController::setSpeedFactor(double k) { m_speed = k; }
bool LoopController::isPaused() const { return m_paused; }
void LoopController::setPaused(bool paused) { m_paused = paused; }
void LoopController::setOut(double pct) { m_model.setOut(pct); }
void LoopController::setSp(double pct) { m_model.setSp(pct); }
void LoopController::setPumpSpeed(double) {}
void LoopController::setHvOpening(double) {}
void LoopController::setParams(const LoopParams &params) { m_model.setParams(params); }

Scenario LoopController::currentScenario() const
{
    return Scenario{};
}

void LoopController::loadScenario(const Scenario &) {}
void LoopController::resetState() {}
void LoopController::tickFor(double) {}
void LoopController::tick() {}

} // namespace mbloop
