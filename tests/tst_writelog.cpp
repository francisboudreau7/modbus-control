#include "WriteLog.h"

#include <QtTest>

using namespace mb;

// Placeholder (T230); T232 writes the tests of WriteLog.h.
class TstWriteLog : public QObject
{
    Q_OBJECT
private slots:
    void defaultFolderEndsWithWriteLogs();
};

void TstWriteLog::defaultFolderEndsWithWriteLogs()
{
    QVERIFY(WriteLogFile::defaultFolder().endsWith(QStringLiteral("Modbus Control/Write logs")));
}

QTEST_GUILESS_MAIN(TstWriteLog)
#include "tst_writelog.moc"
