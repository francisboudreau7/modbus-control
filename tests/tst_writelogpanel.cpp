#include "WriteLogPanel.h"

#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

using namespace mb;

// Placeholder (T230); T233 writes the tests of WriteLogPanel.h.
class TstWriteLogPanel : public QObject
{
    Q_OBJECT
private slots:
    void hasNamedWidgets();
};

void TstWriteLogPanel::hasNamedWidgets()
{
    WriteLogPanel panel;
    QVERIFY(panel.findChild<QTableWidget *>(QStringLiteral("writeLogTable")));
    QVERIFY(panel.findChild<QPushButton *>(QStringLiteral("openFolderButton")));
    QVERIFY(panel.findChild<QPushButton *>(QStringLiteral("clearButton")));
}

QTEST_MAIN(TstWriteLogPanel)
#include "tst_writelogpanel.moc"
