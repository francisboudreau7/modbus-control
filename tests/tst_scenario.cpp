#include "Scenario.h"

#include <QtTest>

class TstScenario : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T210; the implementing task writes the real tests.
    void builds() { QVERIFY(true); }
};

QTEST_MAIN(TstScenario)
#include "tst_scenario.moc"
