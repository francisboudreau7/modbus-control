#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstTagCodec : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_GUILESS_MAIN(TstTagCodec)
#include "tst_tagcodec.moc"