#pragma once

#include "LoopModel.h"
#include "LoopServer.h"
#include "Scenario.h"

#include <QElapsedTimer>
#include <QObject>

#include <functional>

class QTimer;

namespace mbloop {

/// Runs level loop 101: owns the LoopModel and the LoopServer, and a 50 ms QTimer (wall clock).
/// Each tick: applies OUT/SP written by Modbus clients (LoopServer::takeWrites), advances the
/// model by the elapsed wall time (measured with QElapsedTimer, capped at 0.5 s) x the speed
/// factor (nothing while paused), publishes PV/SP/OUT to the server, and emits stateChanged().
/// Used by the window and by the --headless mode. Starts with reset(LoopParams{}, 50), running.
class LoopController : public QObject
{
    Q_OBJECT
public:
    explicit LoopController(quint32 seed = 1, QObject *parent = nullptr);
    ~LoopController() override;

    /// Starts the Modbus server (see LoopServer::start).
    bool startServer(const QString &host, quint16 port, QString *error = nullptr);
    LoopServer &server();
    const LoopModel &model() const;

    double speedFactor() const;    ///< 1, 2, 5 or 10
    void setSpeedFactor(double k); ///< other values are rounded to the nearest allowed one
    bool isPaused() const;
    void setPaused(bool paused);

    /// Faceplate / window actions (also published to the server at once).
    void setOut(double pct);
    void setSp(double pct);
    void setPumpSpeed(double pct);
    void setHvOpening(double pct);
    void setParams(const LoopParams &params);

    /// Scenario of the current settings and state (state always present).
    Scenario currentScenario() const;
    /// applyScenario + speed factor; emits stateChanged().
    void loadScenario(const Scenario &scenario);
    /// Back to the initial state with the current settings (OUT = SP = 50 %).
    void resetState();

    /// Test seam: advances by `wallSeconds` of wall time as one tick would (ignores the timer).
    void tickFor(double wallSeconds);

signals:
    void stateChanged();
    void pausedChanged(bool paused);
    void speedFactorChanged(double k);

private:
    // Implementation-defined; T212 may change the private section freely.
    void tick();
    LoopModel m_model;
    LoopServer m_server;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    double m_speed = 1.0;
    bool m_paused = false;
};

} // namespace mbloop
