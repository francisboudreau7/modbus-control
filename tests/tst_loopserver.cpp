#include "LoopServer.h"

#include <QtTest>

class TstLoopServer : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T210; the implementing task writes the real tests.
    void builds() { QVERIFY(true); }
};

QTEST_MAIN(TstLoopServer)
#include "tst_loopserver.moc"
