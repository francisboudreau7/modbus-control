#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstIntegration : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_GUILESS_MAIN(TstIntegration)
#include "tst_integration.moc"