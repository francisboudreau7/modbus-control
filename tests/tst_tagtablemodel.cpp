#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstTagTableModel : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_MAIN(TstTagTableModel)
#include "tst_tagtablemodel.moc"