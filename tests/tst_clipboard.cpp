#include <QtTest>

// Stub created by T100; filled in by its task.

class Test : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(true); }
};

QTEST_MAIN(Test)
#include "tst_clipboard.moc"
