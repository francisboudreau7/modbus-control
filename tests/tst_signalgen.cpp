#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstSignalGen : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_GUILESS_MAIN(TstSignalGen)
#include "tst_signalgen.moc"