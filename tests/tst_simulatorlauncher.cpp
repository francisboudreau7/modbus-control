// SimulatorLauncher: real process on --port 0, port probe against a local QTcpServer (v1.3.4).
#include "SimulatorLauncher.h"

#include <QHostAddress>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QtTest>

#include <windows.h>

using namespace mb;

namespace {

const QStringList kHeadless{QStringLiteral("--headless"), QStringLiteral("--port"),
                            QStringLiteral("0")};

QString simPath() { return QString::fromUtf8(MC_LOOP_SIM_PATH); }

quint16 closedPort()
{
    QTcpServer s;
    if (!s.listen(QHostAddress::LocalHost, 0))
        return 0;
    const quint16 p = s.serverPort();
    s.close();
    return p;
}

/// Waits on a process handle by pid, independently of QProcess.
struct ProcHandle
{
    HANDLE h = nullptr;
    explicit ProcHandle(qint64 pid)
    {
        if (pid > 0)
            h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    }
    ProcHandle(const ProcHandle &) = delete;
    ProcHandle &operator=(const ProcHandle &) = delete;
    ~ProcHandle()
    {
        if (h)
            CloseHandle(h);
    }
    bool valid() const { return h != nullptr; }
    bool exited(DWORD ms) const { return WaitForSingleObject(h, ms) == WAIT_OBJECT_0; }
};

qint64 pidOf(const SimulatorLauncher &l)
{
    auto *p = l.findChild<QProcess *>();
    return p ? p->processId() : 0;
}

} // namespace

class TstSimulatorLauncher : public QObject
{
    Q_OBJECT
private slots:
    void portProbe()
    {
        SimulatorLauncher l;
        QTcpServer s;
        QVERIFY(s.listen(QHostAddress::LocalHost, 0));
        QVERIFY(l.isPortOpen(s.serverPort()));
        s.close();
        QVERIFY(!l.isPortOpen(closedPort()));
    }

    void startedOnOpenPort()
    {
        QTcpServer s;
        QVERIFY(s.listen(QHostAddress::LocalHost, 0));
        SimulatorLauncher l;
        l.setStopTimeoutMs(300);
        QSignalSpy started(&l, &SimulatorLauncher::started);
        QSignalSpy failed(&l, &SimulatorLauncher::failed);
        l.start(simPath(), kHeadless, s.serverPort());
        QVERIFY(l.isRunning());
        const ProcHandle proc(pidOf(l));
        QVERIFY(proc.valid());
        QVERIFY(started.wait(10000));
        QVERIFY(l.isRunning());
        l.stop();
        QVERIFY(!l.isRunning());
        QVERIFY(proc.exited(0));
        QTest::qWait(400);
        QCOMPARE(started.count(), 1);
        QCOMPARE(failed.count(), 0);
    }

    void nonexistentProgram()
    {
        SimulatorLauncher l;
        QSignalSpy started(&l, &SimulatorLauncher::started);
        QSignalSpy failed(&l, &SimulatorLauncher::failed);
        l.start(QStringLiteral("C:/does/not/exist/nothing.exe"), {}, closedPort());
        QCOMPARE(failed.count(), 0); // reported after start() returned
        QVERIFY(failed.wait(5000));
        QTest::qWait(400);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(started.count(), 0);
        QVERIFY(!l.isRunning());
        QVERIFY(!failed.first().first().toString().isEmpty());
    }

    void exitsAtOnce()
    {
        SimulatorLauncher l;
        QSignalSpy started(&l, &SimulatorLauncher::started);
        QSignalSpy failed(&l, &SimulatorLauncher::failed);
        l.start(simPath(),
                {QStringLiteral("--headless"), QStringLiteral("--speed"), QStringLiteral("9")},
                closedPort());
        QVERIFY(failed.wait(10000));
        QTest::qWait(400);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(started.count(), 0);
        QVERIFY(failed.first().first().toString().contains(QStringLiteral("2")));
        QVERIFY(!l.isRunning());
    }

    void timeout()
    {
        SimulatorLauncher l;
        l.setStartTimeoutMs(800);
        l.setStopTimeoutMs(300);
        QSignalSpy started(&l, &SimulatorLauncher::started);
        QSignalSpy failed(&l, &SimulatorLauncher::failed);
        const quint16 port = closedPort();
        l.start(simPath(), kHeadless, port);
        const ProcHandle proc(pidOf(l));
        QVERIFY(proc.valid());
        QVERIFY(failed.wait(10000));
        QTest::qWait(400);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(started.count(), 0);
        QVERIFY(!l.isRunning());
        QVERIFY(proc.exited(2000));
        const QString reason = failed.first().first().toString();
        QVERIFY2(reason.contains(QString::number(port)), qPrintable(reason));
    }

    void stopCancelsStart()
    {
        SimulatorLauncher l;
        l.setStopTimeoutMs(300);
        l.setStartTimeoutMs(1000);
        QSignalSpy started(&l, &SimulatorLauncher::started);
        QSignalSpy failed(&l, &SimulatorLauncher::failed);
        l.start(simPath(), kHeadless, closedPort());
        QTest::qWait(300);
        l.stop();
        QVERIFY(!l.isRunning());
        QTest::qWait(1500);
        QCOMPARE(started.count(), 0);
        QCOMPARE(failed.count(), 0);
    }

    void destructorStopsProcess()
    {
        QTcpServer s;
        QVERIFY(s.listen(QHostAddress::LocalHost, 0));
        auto *l = new SimulatorLauncher;
        l->setStopTimeoutMs(300);
        QSignalSpy started(l, &SimulatorLauncher::started);
        l->start(simPath(), kHeadless, s.serverPort());
        const ProcHandle proc(pidOf(*l));
        QVERIFY(proc.valid());
        QVERIFY(started.wait(10000));
        QVERIFY(!proc.exited(0));
        delete l;
        QVERIFY(proc.exited(0));
    }

    void startAgainAfterStop()
    {
        QTcpServer s;
        QVERIFY(s.listen(QHostAddress::LocalHost, 0));
        SimulatorLauncher l;
        l.setStopTimeoutMs(300);
        QSignalSpy started(&l, &SimulatorLauncher::started);
        QSignalSpy failed(&l, &SimulatorLauncher::failed);
        for (int i = 1; i <= 2; ++i) {
            l.start(simPath(), kHeadless, s.serverPort());
            QVERIFY(started.wait(10000));
            QCOMPARE(started.count(), i);
            QVERIFY(l.isRunning());
            l.stop();
            QVERIFY(!l.isRunning());
        }
        QTest::qWait(400);
        QCOMPARE(started.count(), 2);
        QCOMPARE(failed.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TstSimulatorLauncher)
#include "tst_simulatorlauncher.moc"
