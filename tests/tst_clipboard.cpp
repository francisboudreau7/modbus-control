#include "ClipboardCopy.h"

#include <QAbstractItemModel>
#include <QClipboard>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QModelIndex>
#include <QShortcut>
#include <QStandardItemModel>
#include <QTableView>
#include <QtTest>

using mb::installCopyShortcut;
using mb::tableToTsv;

namespace {

// 3x3 table: headers A, B, C; cells "1".."9" row by row.
void fillModel(QStandardItemModel &model)
{
    model.setColumnCount(3);
    model.setRowCount(3);
    model.setHorizontalHeaderLabels({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    int n = 1;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            model.setItem(r, c, new QStandardItem(QString::number(n++)));
}

QModelIndex at(const QAbstractItemModel &model, int r, int c)
{
    return model.index(r, c);
}

} // namespace

class Test : public QObject
{
    Q_OBJECT
private slots:
    void emptySelectionCopiesHeaderAndAllRows()
    {
        QStandardItemModel model;
        fillModel(model);
        QCOMPARE(tableToTsv(&model, {}),
                 QStringLiteral("A\tB\tC\r\n1\t2\t3\r\n4\t5\t6\r\n7\t8\t9\r\n"));
    }

    void emptyModelWithEmptySelectionCopiesOnlyHeaders()
    {
        QStandardItemModel model;
        model.setColumnCount(2);
        model.setHorizontalHeaderLabels({QStringLiteral("X"), QStringLiteral("Y")});
        QCOMPARE(tableToTsv(&model, {}), QStringLiteral("X\tY\r\n"));
    }

    void nullModelGivesEmptyText()
    {
        QCOMPARE(tableToTsv(nullptr, {}), QString());
    }

    void singleCell()
    {
        QStandardItemModel model;
        fillModel(model);
        QCOMPARE(tableToTsv(&model, {at(model, 1, 1)}), QStringLiteral("5\r\n"));
    }

    void twoByTwoBlock()
    {
        QStandardItemModel model;
        fillModel(model);
        const QModelIndexList selection{at(model, 0, 0), at(model, 0, 1),
                                        at(model, 1, 0), at(model, 1, 1)};
        QCOMPARE(tableToTsv(&model, selection), QStringLiteral("1\t2\r\n4\t5\r\n"));
    }

    void nonRectangularSelectionFillsBoundingRectangle()
    {
        QStandardItemModel model;
        fillModel(model);
        // (0,0) and (1,2): rows 0-1, columns 0-2; the other cells are empty.
        const QModelIndexList selection{at(model, 0, 0), at(model, 1, 2)};
        QCOMPARE(tableToTsv(&model, selection), QStringLiteral("1\t\t\r\n\t\t6\r\n"));
    }

    void outOfOrderSelectionIsSorted()
    {
        QStandardItemModel model;
        fillModel(model);
        const QModelIndexList reversed{at(model, 1, 1), at(model, 1, 0),
                                       at(model, 0, 1), at(model, 0, 0)};
        QCOMPARE(tableToTsv(&model, reversed), QStringLiteral("1\t2\r\n4\t5\r\n"));

        const QModelIndexList diagonalReversed{at(model, 1, 2), at(model, 0, 0)};
        QCOMPARE(tableToTsv(&model, diagonalReversed), QStringLiteral("1\t\t\r\n\t\t6\r\n"));
    }

    void tabCrLfInsideCellBecomeSpaces()
    {
        QStandardItemModel model;
        fillModel(model);
        model.setItem(0, 0, new QStandardItem(QStringLiteral("x\ty\rz\nw")));
        QCOMPARE(tableToTsv(&model, {at(model, 0, 0)}), QStringLiteral("x y z w\r\n"));
        // Same rule applies to the full-table copy.
        QCOMPARE(tableToTsv(&model, {}),
                 QStringLiteral("A\tB\tC\r\nx y z w\t2\t3\r\n4\t5\t6\r\n7\t8\t9\r\n"));
    }

    void shortcutIsWiredToViewWithCopyKey()
    {
        QStandardItemModel model;
        fillModel(model);
        QTableView view;
        view.setModel(&model);
        installCopyShortcut(&view);

        auto *shortcut = view.findChild<QShortcut *>();
        QVERIFY(shortcut);
        QCOMPARE(shortcut->key(), QKeySequence(QKeySequence::Copy));
        QCOMPARE(shortcut->context(), Qt::WidgetWithChildrenShortcut);
    }

    void shortcutCopiesSelectedCells()
    {
        QStandardItemModel model;
        fillModel(model);
        QTableView view;
        view.setModel(&model);
        installCopyShortcut(&view);

        view.selectionModel()->select(at(model, 0, 1), QItemSelectionModel::Select);
        view.selectionModel()->select(at(model, 1, 2), QItemSelectionModel::Select);

        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        clipboard->clear();

        auto *shortcut = view.findChild<QShortcut *>();
        QVERIFY(shortcut);
        emit shortcut->activated();

        // Rows 0-1, columns 1-2.
        QCOMPARE(clipboard->text(), QStringLiteral("2\t\r\n\t6\r\n"));
    }

    void shortcutWithNothingSelectedCopiesWholeTable()
    {
        QStandardItemModel model;
        fillModel(model);
        QTableView view;
        view.setModel(&model);
        installCopyShortcut(&view);
        view.clearSelection();

        QClipboard *clipboard = QGuiApplication::clipboard();
        QVERIFY(clipboard);
        clipboard->clear();

        auto *shortcut = view.findChild<QShortcut *>();
        QVERIFY(shortcut);
        emit shortcut->activated();

        QCOMPARE(clipboard->text(),
                 QStringLiteral("A\tB\tC\r\n1\t2\t3\r\n4\t5\t6\r\n7\t8\t9\r\n"));
    }
};

QTEST_MAIN(Test)
#include "tst_clipboard.moc"
