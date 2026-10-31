#include "WriteLogPanel.h"

#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QtTest>

using namespace mb;

class TstWriteLogPanel : public QObject
{
    Q_OBJECT
private:
    static WriteLogEntry entry(int n, bool ok = true, int fc = 6)
    {
        WriteLogEntry e;
        e.time = QDateTime(QDate(2026, 10, 31), QTime(14, 3, 12, 250));
        e.tagId = TagId(n);
        e.tagName = QStringLiteral("Tag%1").arg(n);
        e.address = quint16(100 + n);
        e.count = 1;
        e.registers = QStringLiteral("HR %1").arg(100 + n);
        e.functionCode = fc;
        e.oldValue = QStringLiteral("50 %");
        e.newValue = QStringLiteral("-25 %");
        e.raw = QStringLiteral("0xFFE7");
        e.source = QStringLiteral("quick");
        e.ok = ok;
        e.result = ok ? QStringLiteral("OK") : QStringLiteral("Modbus exception 2: Illegal data address");
        e.server = QStringLiteral("192.168.1.20:502 unit 1");
        return e;
    }

    static QTableWidget *table(WriteLogPanel &p)
    {
        return p.findChild<QTableWidget *>(QStringLiteral("writeLogTable"));
    }
    static QLabel *folderLabel(WriteLogPanel &p)
    {
        return p.findChild<QLabel *>(QStringLiteral("folderLabel"));
    }

private slots:
    void hasNamedWidgets()
    {
        WriteLogPanel panel;
        QVERIFY(table(panel));
        QVERIFY(folderLabel(panel));
        QCOMPARE(panel.findChild<QPushButton *>(QStringLiteral("openFolderButton"))->text(), QString("Open folder"));
        QCOMPARE(panel.findChild<QPushButton *>(QStringLiteral("clearButton"))->text(), QString("Clear list"));
    }

    void columnsAndTexts()
    {
        WriteLogPanel panel;
        QTableWidget *t = table(panel);
        QCOMPARE(t->columnCount(), 7);
        QStringList headers;
        for (int c = 0; c < t->columnCount(); ++c)
            headers << t->horizontalHeaderItem(c)->text();
        QCOMPARE(headers, (QStringList{"Time", "Tag", "Register", "FC", "Old \u2192 New", "Raw", "Result"}));
        QCOMPARE(t->editTriggers(), QAbstractItemView::NoEditTriggers);
        QCOMPARE(t->selectionBehavior(), QAbstractItemView::SelectRows);
        QVERIFY(!t->isSortingEnabled());

        panel.addEntry(entry(1));
        panel.addEntry(entry(2, true, 16));
        QCOMPARE(t->rowCount(), 2);
        QCOMPARE(t->item(0, 0)->text(), QString("14:03:12.250"));
        QCOMPARE(t->item(0, 1)->text(), QString("Tag1"));
        QCOMPARE(t->item(0, 2)->text(), QString("HR 101"));
        QCOMPARE(t->item(0, 3)->text(), QString("FC06"));
        QCOMPARE(t->item(0, 4)->text(), QString("50 % \u2192 -25 %"));
        QCOMPARE(t->item(0, 5)->text(), QString("0xFFE7"));
        QCOMPARE(t->item(0, 6)->text(), QString("OK"));
        QCOMPARE(t->item(1, 1)->text(), QString("Tag2")); // newest at the bottom
        QCOMPARE(t->item(1, 3)->text(), QString("FC16"));
        QCOMPARE(panel.entries().size(), 2);
        QCOMPARE(panel.entries().at(1).tagName, QString("Tag2"));
    }

    void failureIsRed()
    {
        WriteLogPanel panel;
        panel.addEntry(entry(1, true));
        panel.addEntry(entry(2, false));
        QTableWidget *t = table(panel);
        QCOMPARE(t->item(1, 6)->foreground().color(), QColor("#c62828"));
        QCOMPARE(t->item(1, 6)->text(), QString("Modbus exception 2: Illegal data address"));
        QVERIFY(t->item(0, 6)->foreground().color() != QColor("#c62828"));
        QVERIFY(t->item(1, 1)->foreground().color() != QColor("#c62828"));
    }

    void rowToolTip()
    {
        WriteLogPanel panel;
        panel.addEntry(entry(1));
        QTableWidget *t = table(panel);
        for (int c = 0; c < t->columnCount(); ++c) {
            const QString tip = t->item(0, c)->toolTip();
            QVERIFY(tip.contains("2026-10-31"));
            QVERIFY(tip.contains("quick"));
            QVERIFY(tip.contains("192.168.1.20:502 unit 1"));
        }
    }

    void clearRemovesRows()
    {
        WriteLogPanel panel;
        panel.addEntry(entry(1));
        panel.addEntry(entry(2));
        panel.findChild<QPushButton *>(QStringLiteral("clearButton"))->click();
        QCOMPARE(table(panel)->rowCount(), 0);
        QVERIFY(panel.entries().isEmpty());
        panel.addEntry(entry(3));
        QCOMPARE(table(panel)->rowCount(), 1);
        panel.clear();
        QCOMPARE(table(panel)->rowCount(), 0);
    }

    void folderLabelAndError()
    {
        WriteLogPanel panel;
        panel.resize(900, 300);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        panel.setFolder(QStringLiteral("C:/Docs/Write logs"));
        QCOMPARE(panel.folder(), QString("C:/Docs/Write logs"));
        QLabel *label = folderLabel(panel);
        QCOMPARE(label->text(), QString("Saved to C:/Docs/Write logs"));
        QCOMPARE(label->toolTip(), QString("Saved to C:/Docs/Write logs"));

        panel.setFileError(QStringLiteral("Cannot write C:/x/writes.csv: access denied"));
        QCOMPARE(label->text(), QString("Cannot write C:/x/writes.csv: access denied"));
        QVERIFY(label->styleSheet().contains("#c62828"));

        panel.setFileError(QString());
        QCOMPARE(label->text(), QString("Saved to C:/Docs/Write logs"));
        QVERIFY(!label->styleSheet().contains("#c62828"));
    }

    void folderLabelElides()
    {
        WriteLogPanel panel;
        const QString path = QStringLiteral("C:/Users/someone/Documents/Modbus Control/Write logs/")
                             + QString(40, QLatin1Char('x')) + QStringLiteral("/a/very/long/folder/name");
        panel.setFolder(path);
        panel.resize(1500, 300);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QLabel *label = folderLabel(panel);
        QCOMPARE(label->toolTip(), QString("Saved to ") + path);
        QCOMPARE(label->text(), QString("Saved to ") + path);
        panel.setFolder(path + QString(3000, QLatin1Char('y')) + QStringLiteral("/end"));
        QVERIFY(label->text().size() < label->toolTip().size()); // elided, whatever the ellipsis
        QVERIFY(label->text().startsWith("Saved"));
        QVERIFY(label->text().endsWith("/end"));
        QVERIFY(label->toolTip().endsWith("yyyy/end")); // full path stays in the tooltip
        QVERIFY(label->toolTip().size() > label->text().size());
    }

    void openFolderSeam()
    {
        WriteLogPanel panel;
        QStringList opened;
        panel.setOpenFolder([&](const QString &f) { opened << f; });
        panel.setFolder(QStringLiteral("D:/logs"));
        panel.findChild<QPushButton *>(QStringLiteral("openFolderButton"))->click();
        QCOMPARE(opened, QStringList{"D:/logs"});
        panel.setFolder(QStringLiteral("D:/other"));
        panel.findChild<QPushButton *>(QStringLiteral("openFolderButton"))->click();
        QCOMPARE(opened, (QStringList{"D:/logs", "D:/other"}));
    }

    void autoscrollOnlyAtBottom()
    {
        WriteLogPanel panel;
        panel.resize(500, 200);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QScrollBar *bar = table(panel)->verticalScrollBar();
        for (int i = 0; i < 40; ++i)
            panel.addEntry(entry(i));
        QVERIFY(bar->maximum() > 0);
        QTRY_COMPARE(bar->value(), bar->maximum()); // followed while at the bottom

        bar->setValue(0); // the user reads older rows
        for (int i = 40; i < 45; ++i)
            panel.addEntry(entry(i));
        QCoreApplication::processEvents();
        QCOMPARE(bar->value(), 0);

        bar->setValue(bar->maximum()); // back to the bottom: follows again
        panel.addEntry(entry(45));
        QTRY_COMPARE(bar->value(), bar->maximum());
    }
};

QTEST_MAIN(TstWriteLogPanel)
#include "tst_writelogpanel.moc"
