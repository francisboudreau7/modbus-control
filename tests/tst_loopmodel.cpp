#include "LoopModel.h"

#include <QtTest>

class TstLoopModel : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T210; the implementing task writes the real tests.
    void builds() { QVERIFY(true); }
};

QTEST_MAIN(TstLoopModel)
#include "tst_loopmodel.moc"
