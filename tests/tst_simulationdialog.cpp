// Simulation catalog and the Load simulation dialog (v1.3.4).
#include "SimulationCatalog.h"
#include "SimulationDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

using namespace mb;

namespace {

QList<SimulationEntry> twoEntries()
{
    return {{QStringLiteral("a"), QStringLiteral("First"), QStringLiteral("First description"),
             QStringLiteral("a.mbloop"), QStringLiteral("a.mbproj")},
            {QStringLiteral("b"), QStringLiteral("Second"), QStringLiteral("Second description"),
             QStringLiteral("b.mbloop"), QStringLiteral("b.mbproj")}};
}

QPushButton *loadButton(SimulationDialog &dialog)
{
    return dialog.findChild<QPushButton *>(QStringLiteral("simulationLoad"));
}

} // namespace

class TstSimulationDialog : public QObject
{
    Q_OBJECT
private slots:
    void catalogIsWellFormed()
    {
        const QList<SimulationEntry> entries = builtInSimulations();
        QVERIFY(!entries.isEmpty());
        QSet<QString> ids;
        for (const SimulationEntry &e : entries) {
            QVERIFY(!e.id.isEmpty() && !e.name.isEmpty() && !e.description.isEmpty());
            QVERIFY(!e.scenarioFile.isEmpty() && !e.projectFile.isEmpty());
            QVERIFY(!ids.contains(e.id));
            ids.insert(e.id);
        }
    }

    void listShowsTheNamesAndPreselectsTheFirst()
    {
        SimulationDialog dialog(twoEntries());
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Load simulation"));
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("simulationList"));
        QVERIFY(list);
        QCOMPARE(list->count(), 2);
        QCOMPARE(list->item(0)->text(), QStringLiteral("First"));
        QCOMPARE(list->item(1)->text(), QStringLiteral("Second"));
        QCOMPARE(dialog.selectedIndex(), 0);
        QPushButton *load = loadButton(dialog);
        QVERIFY(load);
        QCOMPARE(load->text(), QStringLiteral("Load"));
        QVERIFY(load->isEnabled());
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons && buttons->button(QDialogButtonBox::Cancel));
    }

    void descriptionFollowsTheSelection()
    {
        SimulationDialog dialog(twoEntries());
        auto *description = dialog.findChild<QLabel *>(QStringLiteral("simulationDescription"));
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("simulationList"));
        QVERIFY(description && list);
        QVERIFY(description->wordWrap());
        QCOMPARE(description->text(), QStringLiteral("First description"));
        list->setCurrentRow(1);
        QCOMPARE(dialog.selectedIndex(), 1);
        QCOMPARE(description->text(), QStringLiteral("Second description"));
    }

    void singleEntryIsSelected()
    {
        SimulationDialog dialog(twoEntries().mid(0, 1));
        QCOMPARE(dialog.selectedIndex(), 0);
        QVERIFY(loadButton(dialog)->isEnabled());
        auto *description = dialog.findChild<QLabel *>(QStringLiteral("simulationDescription"));
        QCOMPARE(description->text(), QStringLiteral("First description"));
    }

    void emptyListDisablesLoad()
    {
        SimulationDialog dialog({});
        QCOMPARE(dialog.selectedIndex(), -1);
        QVERIFY(!loadButton(dialog)->isEnabled());
    }

    void loadAccepts()
    {
        SimulationDialog dialog(twoEntries());
        dialog.show();
        loadButton(dialog)->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void cancelRejects()
    {
        SimulationDialog dialog(twoEntries());
        dialog.show();
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }

    void doubleClickAccepts()
    {
        SimulationDialog dialog(twoEntries());
        dialog.show();
        auto *list = dialog.findChild<QListWidget *>(QStringLiteral("simulationList"));
        list->setCurrentRow(1);
        Q_EMIT list->itemDoubleClicked(list->item(1));
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.selectedIndex(), 1);
    }
};

QTEST_MAIN(TstSimulationDialog)
#include "tst_simulationdialog.moc"
