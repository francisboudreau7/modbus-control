#include "CsvExporter.h"

#include <QBuffer>
#include <QDate>
#include <QDateTime>
#include <QIODevice>
#include <QLocale>
#include <QRegularExpression>
#include <QTime>
#include <QTimeZone>
#include <QtTest>

#include <cmath>
#include <initializer_list>
#include <limits>

using namespace mb;

namespace {

QDateTime fixedStart()
{
    return QDateTime(QDate(2026, 10, 9), QTime(14, 3, 12, 250),
                     QTimeZone::fromSecondsAheadOfUtc(-4 * 3600));
}

// Builds the expected text by hand (local wall time plus "+hh:mm" offset), so it is correct in
// any local zone without reusing the exporter's expression.
QString expectedTimestamp(const QDateTime &start, double t)
{
    const QDateTime local = start.addMSecs(qRound64(t * 1000.0)).toLocalTime();
    const int offset = local.offsetFromUtc();
    const int minutes = qAbs(offset) / 60;
    return local.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"))
           + QString::asprintf("%c%02d:%02d", offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
}

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

TagSample bad(TagId id, Quality quality = Quality::Timeout)
{
    TagSample s;
    s.tagId = id;
    s.quality = quality;
    return s;
}

SampleBatch batch(double t, std::initializer_list<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    for (const TagSample &s : samples)
        b.samples.append(s);
    return b;
}

CsvColumn column(TagId id, const QString &header)
{
    CsvColumn c;
    c.tagId = id;
    c.header = header;
    return c;
}

// Device that always fails writes, to exercise the error path.
class FailingDevice : public QIODevice
{
public:
    using QIODevice::QIODevice;

protected:
    qint64 readData(char *, qint64) override { return -1; }
    qint64 writeData(const char *, qint64) override { return -1; }
};

} // namespace

class TstCsvExporter : public QObject
{
    Q_OBJECT
private:
    // Runs the exporter into a fresh buffer and returns the bytes written. A failure is
    // reported as a warning; the golden comparison in the test then fails.
    static QByteArray run(const Recorder &recorder, const QList<CsvColumn> &columns,
                          const CsvOptions &options = {})
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QString error;
        if (!exportCsv(buffer, recorder, columns, options, &error))
            qWarning("exportCsv failed: %s", qPrintable(error));
        return buffer.data();
    }

private slots:
    void emptyRecorderWritesHeaderOnly()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());

        const QByteArray out = run(recorder, {column(1, QStringLiteral("Sine [V]"))});

        QCOMPARE(QString::fromUtf8(out), QStringLiteral("timestamp,elapsed_s,Sine [V]\r\n"));
    }

    void twoColumnsWithValues()
    {
        const QDateTime start = fixedStart();
        Recorder recorder(100);
        recorder.setStartTime(start);
        recorder.append(batch(0.0, {good(1, 1.5), good(2, -2.5)}));
        recorder.append(batch(1.5, {good(1, 2.0), good(2, 42.0)}));

        const QByteArray out = run(recorder, {column(1, QStringLiteral("A")),
                                              column(2, QStringLiteral("B"))});

        const QString expected = QStringLiteral("timestamp,elapsed_s,A,B\r\n")
                                 + expectedTimestamp(start, 0.0) + QStringLiteral(",0.000,1.5,-2.5\r\n")
                                 + expectedTimestamp(start, 1.5) + QStringLiteral(",1.500,2,42\r\n");
        QCOMPARE(QString::fromUtf8(out), expected);
    }

    void timestampCarriesUtcOffset()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());
        recorder.append(batch(0.0, {good(1, 1.0)}));

        const QString out = QString::fromUtf8(run(recorder, {column(1, QStringLiteral("A"))}));
        const QString dataLine = out.split(QStringLiteral("\r\n")).at(1);
        const QString timestamp = dataLine.section(u',', 0, 0);

        const QRegularExpression pattern(
            QStringLiteral(R"(^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}([+-]\d\d:\d\d|Z)$)"));
        QVERIFY2(pattern.match(timestamp).hasMatch(), qPrintable(timestamp));
    }

    void nanCellIsEmpty()
    {
        const QDateTime start = fixedStart();
        Recorder recorder(100);
        recorder.setStartTime(start);
        // A non-Good sample is stored as NaN; a Good sample with a NaN value is too.
        recorder.append(batch(0.0, {good(1, 1.0), bad(2)}));
        recorder.append(batch(1.0, {good(1, std::numeric_limits<double>::quiet_NaN()),
                                    good(2, 3.0)}));

        const QByteArray out = run(recorder, {column(1, QStringLiteral("A")),
                                              column(2, QStringLiteral("B"))});

        const QString expected = QStringLiteral("timestamp,elapsed_s,A,B\r\n")
                                 + expectedTimestamp(start, 0.0) + QStringLiteral(",0.000,1,\r\n")
                                 + expectedTimestamp(start, 1.0) + QStringLiteral(",1.000,,3\r\n");
        QCOMPARE(QString::fromUtf8(out), expected);
    }

    void semicolonDelimiter()
    {
        const QDateTime start = fixedStart();
        Recorder recorder(100);
        recorder.setStartTime(start);
        recorder.append(batch(0.5, {good(1, 1.0), good(2, 2.0)}));

        CsvOptions options;
        options.delimiter = u';';
        const QByteArray out = run(recorder, {column(1, QStringLiteral("A")),
                                              column(2, QStringLiteral("B"))}, options);

        const QString expected = QStringLiteral("timestamp;elapsed_s;A;B\r\n")
                                 + expectedTimestamp(start, 0.5) + QStringLiteral(";0.500;1;2\r\n");
        QCOMPARE(QString::fromUtf8(out), expected);
    }

    void headerQuoting()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());

        const QList<CsvColumn> columns = {
            column(1, QStringLiteral("a,b")),
            column(2, QStringLiteral("say \"hi\"")),
            column(3, QStringLiteral("line1\nline2")),
            column(4, QStringLiteral("cr\rx")),
            column(5, QStringLiteral("semi;colon")), // not the delimiter, so not quoted
        };
        const QByteArray out = run(recorder, columns);

        QCOMPARE(QString::fromUtf8(out),
                 QStringLiteral("timestamp,elapsed_s,\"a,b\",\"say \"\"hi\"\"\","
                                "\"line1\nline2\",\"cr\rx\",semi;colon\r\n"));
    }

    void headerQuotingWithSemicolonDelimiter()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());

        CsvOptions options;
        options.delimiter = u';';
        const QByteArray out = run(recorder,
                                   {column(1, QStringLiteral("x;y")), column(2, QStringLiteral("a,b"))},
                                   options);

        QCOMPARE(QString::fromUtf8(out), QStringLiteral("timestamp;elapsed_s;\"x;y\";a,b\r\n"));
    }

    void rangeBoundsAreInclusive()
    {
        const QDateTime start = fixedStart();
        Recorder recorder(100);
        recorder.setStartTime(start);
        for (int i = 0; i <= 4; ++i)
            recorder.append(batch(i, {good(1, i * 10.0)}));

        CsvOptions options;
        options.useRange = true;
        options.t1 = 1.0;
        options.t2 = 3.0;
        const QByteArray out = run(recorder, {column(1, QStringLiteral("A"))}, options);

        const QString expected = QStringLiteral("timestamp,elapsed_s,A\r\n")
                                 + expectedTimestamp(start, 1.0) + QStringLiteral(",1.000,10\r\n")
                                 + expectedTimestamp(start, 2.0) + QStringLiteral(",2.000,20\r\n")
                                 + expectedTimestamp(start, 3.0) + QStringLiteral(",3.000,30\r\n");
        QCOMPARE(QString::fromUtf8(out), expected);
    }

    void emptyRangeWritesHeaderOnly()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());
        recorder.append(batch(0.0, {good(1, 1.0)}));
        recorder.append(batch(1.0, {good(1, 2.0)}));

        CsvOptions options;
        options.useRange = true;

        // Range that contains no rows.
        options.t1 = 5.0;
        options.t2 = 10.0;
        QCOMPARE(QString::fromUtf8(run(recorder, {column(1, QStringLiteral("A"))}, options)),
                 QStringLiteral("timestamp,elapsed_s,A\r\n"));

        // Reversed range.
        options.t1 = 1.0;
        options.t2 = 0.0;
        QCOMPARE(QString::fromUtf8(run(recorder, {column(1, QStringLiteral("A"))}, options)),
                 QStringLiteral("timestamp,elapsed_s,A\r\n"));
    }

    void columnsFollowCallerOrder()
    {
        const QDateTime start = fixedStart();
        Recorder recorder(100);
        recorder.setStartTime(start);
        recorder.append(batch(0.0, {good(1, 1.0), good(2, 2.0)}));

        // Tag 99 is unknown to the recorder and must give empty cells.
        const QByteArray out = run(recorder, {column(2, QStringLiteral("B")),
                                              column(99, QStringLiteral("X")),
                                              column(1, QStringLiteral("A"))});

        const QString expected = QStringLiteral("timestamp,elapsed_s,B,X,A\r\n")
                                 + expectedTimestamp(start, 0.0) + QStringLiteral(",0.000,2,,1\r\n");
        QCOMPARE(QString::fromUtf8(out), expected);
    }

    void valueFormatting()
    {
        const QDateTime start = fixedStart();
        Recorder recorder(100);
        recorder.setStartTime(start);
        recorder.append(batch(0.0, {good(1, 0.1), good(2, 1e-7), good(3, -2.5),
                                    good(4, 123456789.0)}));

        const QList<CsvColumn> columns = {
            column(1, QStringLiteral("a")), column(2, QStringLiteral("b")),
            column(3, QStringLiteral("c")), column(4, QStringLiteral("d")),
        };

        // Decimal separator must stay '.' even under a comma-decimal locale.
        const QLocale previous = QLocale();
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        const QByteArray out = run(recorder, columns);
        QLocale::setDefault(previous);

        const QString expected = QStringLiteral("timestamp,elapsed_s,a,b,c,d\r\n")
                                 + expectedTimestamp(start, 0.0)
                                 + QStringLiteral(",0.000,0.1,1e-07,-2.5,123456789\r\n");
        QCOMPARE(QString::fromUtf8(out), expected);
    }

    void invalidStartTimeGivesEmptyTimestamp()
    {
        Recorder recorder(100); // no setStartTime(): invalid
        recorder.append(batch(1.25, {good(1, 7.0)}));

        const QByteArray out = run(recorder, {column(1, QStringLiteral("A"))});

        QCOMPARE(QString::fromUtf8(out),
                 QStringLiteral("timestamp,elapsed_s,A\r\n,1.250,7\r\n"));
    }

    void closedDeviceFails()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());
        recorder.append(batch(0.0, {good(1, 1.0)}));

        QBuffer buffer; // never opened
        QString error;
        QVERIFY(!exportCsv(buffer, recorder, {column(1, QStringLiteral("A"))}, {}, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(buffer.data().isEmpty());
    }

    void readOnlyDeviceFails()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());

        QBuffer buffer;
        buffer.setData(QByteArray("existing"));
        buffer.open(QIODevice::ReadOnly);
        QString error;
        QVERIFY(!exportCsv(buffer, recorder, {column(1, QStringLiteral("A"))}, {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void failedWriteReportsError()
    {
        Recorder recorder(100);
        recorder.setStartTime(fixedStart());

        FailingDevice device;
        device.open(QIODevice::WriteOnly);
        QString error;
        QVERIFY(!exportCsv(device, recorder, {column(1, QStringLiteral("A"))}, {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void utf8WithoutBomAndCrlfLineEndings()
    {
        const QDateTime start = fixedStart();
        Recorder recorder(100);
        recorder.setStartTime(start);
        recorder.append(batch(0.0, {good(1, 21.5)}));
        recorder.append(batch(0.5, {good(1, 22.0)}));

        const QByteArray out = run(recorder, {column(1, QStringLiteral("Temp °C"))});

        // No BOM; the degree sign is UTF-8 encoded (C2 B0).
        QVERIFY(!out.startsWith("\xEF\xBB\xBF"));
        QCOMPARE(out, QStringLiteral("timestamp,elapsed_s,Temp °C\r\n"
                                     "%1,0.000,21.5\r\n"
                                     "%2,0.500,22\r\n")
                          .arg(expectedTimestamp(start, 0.0), expectedTimestamp(start, 0.5))
                          .toUtf8());

        // Every line break is CRLF: no bare LF.
        QCOMPARE(out.count('\n'), qsizetype(3));
        QCOMPARE(out.count("\r\n"), qsizetype(3));
        QVERIFY(out.endsWith("\r\n"));
    }
};

QTEST_GUILESS_MAIN(TstCsvExporter)
#include "tst_csvexporter.moc"
