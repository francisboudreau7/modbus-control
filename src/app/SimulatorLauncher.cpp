#include "SimulatorLauncher.h"

namespace mb {

// Stub until the launcher task.
SimulatorLauncher::SimulatorLauncher(QObject *parent) : QObject(parent) {}
SimulatorLauncher::~SimulatorLauncher() = default;
bool SimulatorLauncher::isRunning() const { return false; }
bool SimulatorLauncher::isPortOpen(quint16) const { return false; }
void SimulatorLauncher::start(const QString &, const QStringList &, quint16) {}
void SimulatorLauncher::stop() {}
void SimulatorLauncher::setStartTimeoutMs(int ms) { m_startTimeoutMs = ms; }
void SimulatorLauncher::setStopTimeoutMs(int ms) { m_stopTimeoutMs = ms; }

} // namespace mb
