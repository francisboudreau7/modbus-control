#include "AnalysisPanel.h"

#include <QtTest>

using namespace mb;

class TstAnalysisPanel : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T190; T194 writes the real tests.
    void constructs()
    {
        AnalysisPanel panel;
        QVERIFY(panel.results().isEmpty());
        QVERIFY(panel.settings().showModel);
    }
};

QTEST_MAIN(TstAnalysisPanel)
#include "tst_analysispanel.moc"
