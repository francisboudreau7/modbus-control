#include "WriteLog.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace mb;

namespace {

WriteLogEntry sampleEntry()
{
    WriteLogEntry e;
    e.time = QDateTime(QDate(2026, 10, 31), QTime(14, 3, 12, 250));
    e.tagId = 7;
    e.tagName = QStringLiteral("Setpoint");
    e.address = 5008;
    e.count = 2;
    e.registers = QStringLiteral("HR 5009-5010");
    e.functionCode = 16;
    e.oldValue = QStringLiteral("50 %");
    e.newValue = QStringLiteral("-25 %");
    e.raw = QStringLiteral("0xC1C8 0x0000");
    e.source = QStringLiteral("table");
    e.ok = true;
    e.result = QStringLiteral("OK");
    e.server = QStringLiteral("192.168.1.20:502 unit 1");
    return e;
}

// "+hh:mm" / "-hh:mm" offset of the local time zone at `t`, as the CSV shows it.
QString offsetText(const QDateTime &t)
{
    const int seconds = t.offsetFromUtc();
    const int minutes = qAbs(seconds) / 60;
    return QStringLiteral("%1%2:%3")
        .arg(seconds < 0 ? QStringLiteral("-") : QStringLiteral("+"))
        .arg(minutes / 60, 2, 10, QLatin1Char('0'))
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TstWriteLog : public QObject
{
    Q_OBJECT
private slots:
    void defaultFolderEndsWithWriteLogs();
    void headerOrder();
    void csvLineNormalEntry();
    void csvLineQuotesDelimiterQuoteAndNewline();
    void csvLineSemicolonDelimiter();
    void timeHasOffsetAndMilliseconds();
    void fileName();
    void appendCreatesFolderAndFileWithBomHeaderAndLine();
    void secondAppendAddsOnlyLine();
    void otherDateGoesToOtherFile();
    void emptyExistingFileGetsHeader();
    void appendFailsWhenFolderIsAFile();
};

void TstWriteLog::defaultFolderEndsWithWriteLogs()
{
    QVERIFY(WriteLogFile::defaultFolder().endsWith(QStringLiteral("Modbus Control/Write logs")));
}

void TstWriteLog::headerOrder()
{
    const QStringList expected = {
        QStringLiteral("time"),          QStringLiteral("tag"),    QStringLiteral("tag_id"),
        QStringLiteral("registers"),     QStringLiteral("function_code"),
        QStringLiteral("old_value"),     QStringLiteral("new_value"),
        QStringLiteral("raw"),           QStringLiteral("source"), QStringLiteral("result"),
        QStringLiteral("server"),
    };
    QCOMPARE(writeLogHeader(), expected);
}

void TstWriteLog::csvLineNormalEntry()
{
    const QString line = writeLogCsvLine(sampleEntry());
    const QStringList fields = line.split(QLatin1Char(','));
    QCOMPARE(int(fields.size()), 11);

    const QDateTime local = sampleEntry().time;
    QCOMPARE(fields.at(0), QStringLiteral("2026-10-31T14:03:12.250") + offsetText(local));
    QCOMPARE(fields.at(1), QStringLiteral("Setpoint"));
    QCOMPARE(fields.at(2), QStringLiteral("7"));
    QCOMPARE(fields.at(3), QStringLiteral("HR 5009-5010"));
    QCOMPARE(fields.at(4), QStringLiteral("16"));
    QCOMPARE(fields.at(5), QStringLiteral("50 %"));
    QCOMPARE(fields.at(6), QStringLiteral("-25 %"));
    QCOMPARE(fields.at(7), QStringLiteral("0xC1C8 0x0000"));
    QCOMPARE(fields.at(8), QStringLiteral("table"));
    QCOMPARE(fields.at(9), QStringLiteral("OK"));
    QCOMPARE(fields.at(10), QStringLiteral("192.168.1.20:502 unit 1"));
    QVERIFY(!line.endsWith(QLatin1Char('\n')));
    QVERIFY(!line.endsWith(QLatin1Char('\r')));
}

void TstWriteLog::csvLineQuotesDelimiterQuoteAndNewline()
{
    WriteLogEntry e = sampleEntry();
    e.tagName = QStringLiteral("Flow, main");
    e.result = QStringLiteral("Exception 2: \"Illegal\" address");
    e.server = QStringLiteral("line1\nline2");
    const QString line = writeLogCsvLine(e);

    QVERIFY(line.contains(QStringLiteral(",\"Flow, main\",")));
    QVERIFY(line.contains(QStringLiteral(",\"Exception 2: \"\"Illegal\"\" address\",")));
    QVERIFY(line.endsWith(QStringLiteral(",\"line1\nline2\"")));

    // Plain fields are not quoted.
    QVERIFY(line.contains(QStringLiteral(",7,HR 5009-5010,16,50 %,-25 %,0xC1C8 0x0000,table,")));
}

void TstWriteLog::csvLineSemicolonDelimiter()
{
    WriteLogEntry e = sampleEntry();
    e.tagName = QStringLiteral("A;B");
    e.registers = QStringLiteral("HR 1,2");
    const QString line = writeLogCsvLine(e, QLatin1Char(';'));

    // The ';' in the tag name is quoted; a ',' is not special with this delimiter.
    QVERIFY(line.contains(QStringLiteral(";\"A;B\";7;HR 1,2;16;")));
    QVERIFY(line.endsWith(QStringLiteral(";192.168.1.20:502 unit 1")));
}

void TstWriteLog::timeHasOffsetAndMilliseconds()
{
    const QString line = writeLogCsvLine(sampleEntry());
    const QString stamp = line.section(QLatin1Char(','), 0, 0);

    QVERIFY(stamp.startsWith(QStringLiteral("2026-10-31T14:03:12.250")));
    // Offset as [+-]hh:mm at the end, matching the machine's local zone.
    const QString offset = stamp.right(6);
    QVERIFY(offset.at(0) == QLatin1Char('+') || offset.at(0) == QLatin1Char('-'));
    QCOMPARE(offset.at(3), QLatin1Char(':'));
    QCOMPARE(stamp, QStringLiteral("2026-10-31T14:03:12.250") + offsetText(sampleEntry().time));
}

void TstWriteLog::fileName()
{
    QCOMPARE(writeLogFileName(QDate(2026, 10, 31)), QStringLiteral("writes-2026-10-31.csv"));
    QCOMPARE(writeLogFileName(QDate(2027, 1, 5)), QStringLiteral("writes-2027-01-05.csv"));
}

void TstWriteLog::appendCreatesFolderAndFileWithBomHeaderAndLine()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString folder = QDir(tmp.path()).filePath(QStringLiteral("nested/logs"));
    WriteLogFile log(folder);
    QCOMPARE(log.folder(), folder);

    const WriteLogEntry e = sampleEntry();
    QString error;
    QVERIFY(log.append(e, &error));
    QVERIFY(error.isEmpty());

    const QString path = log.pathFor(QDate(2026, 10, 31));
    QVERIFY(path.endsWith(QStringLiteral("writes-2026-10-31.csv")));
    QVERIFY(QFileInfo::exists(path));

    const QByteArray expected = QByteArrayLiteral("\xEF\xBB\xBF")
                                + writeLogHeader().join(QLatin1Char(',')).toUtf8()
                                + QByteArrayLiteral("\r\n")
                                + writeLogCsvLine(e).toUtf8() + QByteArrayLiteral("\r\n");
    QCOMPARE(readAll(path), expected);
}

void TstWriteLog::secondAppendAddsOnlyLine()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    WriteLogFile log(tmp.path());

    WriteLogEntry first = sampleEntry();
    WriteLogEntry second = sampleEntry();
    second.tagName = QStringLiteral("Other");
    second.tagId = 9;
    second.time = QDateTime(QDate(2026, 10, 31), QTime(15, 0, 0, 5));

    QVERIFY(log.append(first));
    QVERIFY(log.append(second));

    const QByteArray expected = QByteArrayLiteral("\xEF\xBB\xBF")
                                + writeLogHeader().join(QLatin1Char(',')).toUtf8()
                                + QByteArrayLiteral("\r\n")
                                + writeLogCsvLine(first).toUtf8() + QByteArrayLiteral("\r\n")
                                + writeLogCsvLine(second).toUtf8() + QByteArrayLiteral("\r\n");
    const QByteArray content = readAll(log.pathFor(QDate(2026, 10, 31)));
    QCOMPARE(content, expected);
    QCOMPARE(int(content.count("time,")), 1); // header written once
    QCOMPARE(int(content.count("\r\n")), 3);
    QCOMPARE(int(content.count('\n')), 3); // every line break is CRLF
}

void TstWriteLog::otherDateGoesToOtherFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    WriteLogFile log(tmp.path());

    WriteLogEntry today = sampleEntry();
    WriteLogEntry tomorrow = sampleEntry();
    tomorrow.tagName = QStringLiteral("Next day");
    tomorrow.time = QDateTime(QDate(2026, 11, 1), QTime(8, 30, 0, 0));

    QVERIFY(log.append(today));
    const QByteArray before = readAll(log.pathFor(QDate(2026, 10, 31)));

    QVERIFY(log.append(tomorrow));
    QVERIFY(QFileInfo::exists(log.pathFor(QDate(2026, 11, 1))));
    QVERIFY(log.pathFor(QDate(2026, 11, 1)).endsWith(QStringLiteral("writes-2026-11-01.csv")));

    // The first day's file is untouched; the second day's file starts with its own header.
    QCOMPARE(readAll(log.pathFor(QDate(2026, 10, 31))), before);
    const QByteArray nextDay = readAll(log.pathFor(QDate(2026, 11, 1)));
    QVERIFY(nextDay.startsWith(QByteArrayLiteral("\xEF\xBB\xBF") + writeLogHeader().join(QLatin1Char(',')).toUtf8()));
    QVERIFY(nextDay.contains(writeLogCsvLine(tomorrow).toUtf8()));
    QVERIFY(!nextDay.contains("Setpoint"));
}

void TstWriteLog::emptyExistingFileGetsHeader()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    WriteLogFile log(tmp.path());
    const QString path = log.pathFor(QDate(2026, 10, 31));
    {
        QFile empty(path);
        QVERIFY(empty.open(QIODevice::WriteOnly));
    }

    const WriteLogEntry e = sampleEntry();
    QVERIFY(log.append(e));

    const QByteArray expected = QByteArrayLiteral("\xEF\xBB\xBF")
                                + writeLogHeader().join(QLatin1Char(',')).toUtf8()
                                + QByteArrayLiteral("\r\n")
                                + writeLogCsvLine(e).toUtf8() + QByteArrayLiteral("\r\n");
    QCOMPARE(readAll(path), expected);
}

void TstWriteLog::appendFailsWhenFolderIsAFile()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString blocker = QDir(tmp.path()).filePath(QStringLiteral("not-a-folder.txt"));
    {
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("x");
    }

    WriteLogFile log(blocker);
    QString error;
    QVERIFY(!log.append(sampleEntry(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(blocker), qPrintable(error));
    // The blocking file is left alone.
    QCOMPARE(readAll(blocker), QByteArrayLiteral("x"));

    // error is optional.
    QVERIFY(!log.append(sampleEntry()));
}

QTEST_GUILESS_MAIN(TstWriteLog)
#include "tst_writelog.moc"
