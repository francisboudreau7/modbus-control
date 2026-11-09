#include "SimulatorLauncher.h"

#include <QHostAddress>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>

namespace mb {

SimulatorLauncher::SimulatorLauncher(QObject *parent) : QObject(parent) {}

SimulatorLauncher::~SimulatorLauncher()
{
    SimulatorLauncher::stop();
}

bool SimulatorLauncher::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool SimulatorLauncher::isPortOpen(quint16 port) const
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    const bool ok = socket.waitForConnected(300);
    socket.abort();
    return ok;
}

void SimulatorLauncher::start(const QString &program, const QStringList &arguments, quint16 port)
{
    // Precondition: !isRunning(). Drop any leftover (finished) process object and watch.
    endWatch();
    releaseProcess(true);
    ++m_generation;
    m_pending = true;
    m_port = port;

    m_process = new QProcess(this);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        onProcessError(static_cast<int>(e));
    });
    connect(m_process, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) { onProcessFinished(code); });

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(200);
        connect(m_timer, &QTimer::timeout, this, &SimulatorLauncher::tick);
    }
    m_elapsed.start();
    m_process->start(program, arguments);
    if (m_pending && m_process && m_process->state() != QProcess::NotRunning)
        m_timer->start();
}

void SimulatorLauncher::stop()
{
    ++m_generation;
    m_pending = false;
    endWatch();
    releaseProcess(true);
}

void SimulatorLauncher::setStartTimeoutMs(int ms) { m_startTimeoutMs = ms; }
void SimulatorLauncher::setStopTimeoutMs(int ms) { m_stopTimeoutMs = ms; }

void SimulatorLauncher::endWatch()
{
    if (m_timer)
        m_timer->stop();
    if (m_probe) {
        QTcpSocket *probe = m_probe;
        m_probe = nullptr;
        probe->disconnect(this);
        probe->abort();
        probe->deleteLater();
    }
}

void SimulatorLauncher::releaseProcess(bool stopIt)
{
    if (!m_process)
        return;
    QProcess *proc = m_process;
    m_process = nullptr;
    proc->disconnect(this);
    if (stopIt && proc->state() != QProcess::NotRunning) {
        proc->terminate();
        if (!proc->waitForFinished(m_stopTimeoutMs)) {
            proc->kill();
            proc->waitForFinished(m_stopTimeoutMs);
        }
    }
    // May be called from the process's own signal: delete later (the parent frees it at the
    // latest).
    proc->deleteLater();
}

void SimulatorLauncher::fail(const QString &reason)
{
    if (!m_pending)
        return;
    m_pending = false;
    endWatch();
    releaseProcess(true);
    emit failed(reason);
}

void SimulatorLauncher::tick()
{
    if (!m_pending)
        return;
    if (m_elapsed.elapsed() >= m_startTimeoutMs) {
        const double seconds = m_startTimeoutMs / 1000.0;
        fail(tr("The Loop Simulator did not start listening on port %1 within %2 s.")
                 .arg(m_port)
                 .arg(seconds, 0, 'g', 3));
        return;
    }
    // A closed local port may take 1-2 s to refuse on Windows: drop an attempt that is not
    // connected yet and try again.
    if (m_probe) {
        QTcpSocket *old = m_probe;
        m_probe = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    m_probe = new QTcpSocket(this);
    connect(m_probe, &QTcpSocket::connected, this, &SimulatorLauncher::onProbeConnected);
    m_probe->connectToHost(QHostAddress::LocalHost, m_port);
}

void SimulatorLauncher::onProbeConnected()
{
    if (!m_pending)
        return;
    m_pending = false;
    endWatch();
    emit started();
}

void SimulatorLauncher::onProcessError(int error)
{
    if (error != QProcess::FailedToStart || !m_pending || !m_process)
        return;
    // Reported possibly from inside start(): fail from the event loop instead.
    const QString reason =
        tr("The Loop Simulator could not be started: %1").arg(m_process->errorString());
    const quint64 generation = m_generation;
    if (m_timer)
        m_timer->stop();
    QTimer::singleShot(0, this, [this, reason, generation] {
        if (generation == m_generation)
            fail(reason);
    });
}

void SimulatorLauncher::onProcessFinished(int exitCode)
{
    if (m_pending) {
        fail(tr("The Loop Simulator closed before it started listening (exit code %1).")
                 .arg(exitCode));
    } else {
        // Exited after started(): silently not running any more.
        releaseProcess(false);
    }
}

} // namespace mb
