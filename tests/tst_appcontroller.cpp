#include <QtTest>

// Stub created by T140; filled in by T141.

class TstAppController : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(true); }
};

QTEST_MAIN(TstAppController)
#include "tst_appcontroller.moc"
