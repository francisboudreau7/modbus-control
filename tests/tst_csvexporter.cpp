#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstCsvExporter : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_GUILESS_MAIN(TstCsvExporter)
#include "tst_csvexporter.moc"