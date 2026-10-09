#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstProjectIO : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_GUILESS_MAIN(TstProjectIO)
#include "tst_projectio.moc"