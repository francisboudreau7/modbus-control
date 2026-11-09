#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTcpSocket;
class QTimer;

namespace mb {

/// Starts and stops the Loop Simulator process for Simulation > Load simulation... (v1.3.4).
/// At most one process at a time; the destructor stops it, so a simulator opened by Modbus
/// Control closes with it. A simulator started some other way is never touched. The functions
/// are virtual so AppController's tests can use a fake and never start a process.
class SimulatorLauncher : public QObject
{
    Q_OBJECT
public:
    explicit SimulatorLauncher(QObject *parent = nullptr);
    ~SimulatorLauncher() override;

    /// True while the process started by start() is running (from start() on, also before
    /// started() was emitted).
    virtual bool isRunning() const;

    /// True when something accepts TCP connections on 127.0.0.1:`port` right now. Blocks for at
    /// most about 300 ms (a listening local port answers at once).
    virtual bool isPortOpen(quint16 port) const;

    /// Starts `program` with `arguments` (precondition: !isRunning()) and watches
    /// 127.0.0.1:`port`, trying a TCP connection every 200 ms without blocking the event loop.
    /// Emits exactly one of:
    /// - started(), when a connection succeeded (the test connection is closed at once);
    /// - failed(reason), when the process cannot start, exits before the port opened, or the
    ///   port is still closed after the start timeout (the process is then stopped).
    /// Reasons are translated full sentences, e.g. "The Loop Simulator did not start listening
    /// on port 1601 within 10 s."
    virtual void start(const QString &program, const QStringList &arguments, quint16 port);

    /// Closes the process and waits: QProcess::terminate (closes its window), then kill after
    /// the stop timeout. Blocking. Cancels a start in progress: neither started() nor failed()
    /// is emitted afterwards. Nothing happens when no process is running.
    virtual void stop();

    /// Default 10000. For tests.
    void setStartTimeoutMs(int ms);
    /// Default 3000. For tests (a headless or offscreen process has no window to close).
    void setStopTimeoutMs(int ms);

signals:
    void started();
    void failed(const QString &reason);

private:
    // Implementation-defined; the task may change the private section freely.
    void tick();
    void onProbeConnected();
    void onProcessError(int error);
    void onProcessFinished(int exitCode);
    void endWatch();
    void releaseProcess(bool stopIt);
    void fail(const QString &reason);

    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    QTcpSocket *m_probe = nullptr;
    QElapsedTimer m_elapsed;
    quint16 m_port = 0;
    bool m_pending = false;
    quint64 m_generation = 0;
    int m_startTimeoutMs = 10000;
    int m_stopTimeoutMs = 3000;
};

} // namespace mb
