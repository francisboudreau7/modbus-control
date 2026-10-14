#include "Tuning.h"

#include <QtTest>

using namespace mb;

class TstTuning : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T190; T192 writes the real tests.
    void names()
    {
        QCOMPARE(toString(TuningRule::CohenCoon), QStringLiteral("Cohen-Coon"));
        QCOMPARE(effectiveLambda(2.0, 0.0), 2.0);
    }
};

QTEST_MAIN(TstTuning)
#include "tst_tuning.moc"
