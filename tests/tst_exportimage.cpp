#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstExportImage : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_MAIN(TstExportImage)
#include "tst_exportimage.moc"