#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstBlockPlanner : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_GUILESS_MAIN(TstBlockPlanner)
#include "tst_blockplanner.moc"