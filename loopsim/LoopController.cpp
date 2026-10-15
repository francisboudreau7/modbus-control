#include "LoopController.h"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace mbloop {

namespace {

constexpr double kMaxTickWallS = 0.5;
constexpr double kSpeeds[] = {1.0, 2.0, 5.0};

double nearestSpeed(double k)
{
    double best = kSpeeds[0];
    for (const double s : kSpeeds) {
        if (std::abs(s - k) < std::abs(best - k)) {
            best = s;
        }
    }
    return best;
}

} // namespace

LoopController::LoopController(quint32 seed, QObject *parent) : QObject(parent), m_model(seed)
{
    m_model.reset(LoopParams{}, 50.0);
    publish();
    m_timer = new QTimer(this);
    m_timer->setInterval(50);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, [this] { tick(); });
    m_clock.start();
    m_timer->start();
}

LoopController::~LoopController()
{
    if (m_timer) {
        m_timer->stop();
    }
    m_server.stop();
}

bool LoopController::startServer(const QString &host, quint16 port, QString *error)
{
    const bool ok = m_server.start(host, port, error);
    if (ok) {
        publish();
    }
    return ok;
}

LoopServer &LoopController::server() { return m_server; }
const LoopModel &LoopController::model() const { return m_model; }
double LoopController::speedFactor() const { return m_speed; }

void LoopController::setSpeedFactor(double k)
{
    const double s = nearestSpeed(k);
    if (s == m_speed) {
        return;
    }
    m_speed = s;
    emit speedFactorChanged(m_speed);
}

bool LoopController::isPaused() const { return m_paused; }

void LoopController::setPaused(bool paused)
{
    if (paused == m_paused) {
        return;
    }
    m_paused = paused;
    m_lastNs = m_clock.nsecsElapsed(); // no catch-up of the time spent paused
    emit pausedChanged(m_paused);
}

void LoopController::setOut(double pct)
{
    applyClientWrites(); // an earlier client write must not overwrite this one on the next tick
    m_model.setOut(pct);
    publish();
}

void LoopController::setSp(double pct)
{
    applyClientWrites();
    m_model.setSp(pct);
    publish();
}

void LoopController::setPumpSpeed(double pct)
{
    LoopParams p = m_model.params();
    p.pumpSpeedPct = pct;
    m_model.setParams(p);
}

void LoopController::setHvOpening(double pct)
{
    LoopParams p = m_model.params();
    p.hvOpeningPct = pct;
    m_model.setParams(p);
}

void LoopController::setParams(const LoopParams &params) { m_model.setParams(params); }

Scenario LoopController::currentScenario() const
{
    Scenario s;
    s.params = m_model.params();
    s.speedFactor = m_speed;
    const LoopState &st = m_model.state();
    Scenario::State state;
    state.levelPct = 100.0 * st.levelM / loop101::kTankHeightM;
    state.outPct = st.outPct;
    state.spPct = st.spPct;
    state.stemPct = st.stemPct;
    s.state = state;
    return s;
}

void LoopController::loadScenario(const Scenario &scenario)
{
    m_server.takeWrites(); // a client write not yet applied must not override the scenario
    applyScenario(m_model, scenario);
    setSpeedFactor(scenario.speedFactor);
    publish();
    emit stateChanged();
}

void LoopController::resetState()
{
    m_server.takeWrites(); // a client write not yet applied must not override the reset
    m_model.reset(m_model.params(), 50.0);
    publish();
    emit stateChanged();
}

void LoopController::tickFor(double wallSeconds)
{
    applyClientWrites();
    if (!m_paused && wallSeconds > 0.0) {
        m_model.advance(wallSeconds * m_speed);
    }
    publish();
    emit stateChanged();
}

void LoopController::tick()
{
    // Nanosecond differences: QElapsedTimer::restart() returns whole milliseconds and drops the
    // remainder, which made the simulated clock run 1-2 % slow at the 50 ms tick.
    const qint64 now = m_clock.nsecsElapsed();
    const double wall = static_cast<double>(now - m_lastNs) / 1e9;
    m_lastNs = now;
    tickFor(std::min(wall, kMaxTickWallS));
}

void LoopController::applyClientWrites()
{
    const LoopServer::Writes w = m_server.takeWrites();
    if (w.outWritten) {
        m_model.setOut(w.out);
    }
    if (w.spWritten) {
        m_model.setSp(w.sp);
    }
}

void LoopController::publish()
{
    const LoopState &st = m_model.state();
    m_server.publish(static_cast<float>(st.pvPct), static_cast<float>(st.spPct), static_cast<float>(st.outPct));
}

} // namespace mbloop
