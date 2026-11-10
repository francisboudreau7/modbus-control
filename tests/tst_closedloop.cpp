// v1.4.0 placeholder; the task that implements the module replaces it.
#include <QtTest>

class TstClosedLoop : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(true); }
};

QTEST_GUILESS_MAIN(TstClosedLoop)
#include "tst_closedloop.moc"
