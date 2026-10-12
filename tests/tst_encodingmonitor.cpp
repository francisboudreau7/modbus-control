#include <QtTest>

// Stub created by T150; filled in by its task.

class TstEncodingMonitor : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(true); }
};

QTEST_MAIN(TstEncodingMonitor)
#include "tst_encodingmonitor.moc"
