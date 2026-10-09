#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstRecorder : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_GUILESS_MAIN(TstRecorder)
#include "tst_recorder.moc"