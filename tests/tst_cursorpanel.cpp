#include <QtTest>

// Stub: implemented by its task (see TASKS.md).
class TstCursorPanel : public QObject
{
    Q_OBJECT
private slots:
    void notImplemented() { QSKIP("not implemented yet"); }
};

QTEST_MAIN(TstCursorPanel)
#include "tst_cursorpanel.moc"