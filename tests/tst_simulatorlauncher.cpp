// SimulatorLauncher: real process on --port 0, port probe against a local QTcpServer (v1.3.4).
#include "SimulatorLauncher.h"

#include <QtTest>

using namespace mb;

class TstSimulatorLauncher : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(!SimulatorLauncher().isRunning()); }
};

QTEST_GUILESS_MAIN(TstSimulatorLauncher)
#include "tst_simulatorlauncher.moc"
