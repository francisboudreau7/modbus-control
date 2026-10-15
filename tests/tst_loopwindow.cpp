#include "LoopWindow.h"

#include <QtTest>

class TstLoopWindow : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T210; the implementing task writes the real tests.
    void builds() { QVERIFY(true); }
};

QTEST_MAIN(TstLoopWindow)
#include "tst_loopwindow.moc"
