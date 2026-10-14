#include "AddTagDialog.h"

#include <QtTest>

using namespace mb;

class TstAddTagDialog : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T180; T183 writes the real tests.
    void constructs()
    {
        AddTagDialog dialog({}, 0, std::nullopt);
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Add tag"));
        QVERIFY(!dialog.detectionConfirmed());
    }
};

QTEST_MAIN(TstAddTagDialog)
#include "tst_addtagdialog.moc"
