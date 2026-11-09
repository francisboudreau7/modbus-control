// Simulation catalog and the Load simulation dialog (v1.3.4).
#include "SimulationCatalog.h"
#include "SimulationDialog.h"

#include <QtTest>

using namespace mb;

class TstSimulationDialog : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(!builtInSimulations().isEmpty()); }
};

QTEST_MAIN(TstSimulationDialog)
#include "tst_simulationdialog.moc"
