#include "SessionIO.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <QElapsedTimer>

#include <bit>
#include <cmath>
#include <limits>

using namespace mb;

namespace {

const double kNan = std::numeric_limits<double>::quiet_NaN();

Project sampleProject()
{
    Project p;
    p.connection.host = QStringLiteral("10.0.0.5");
    p.connection.port = 1502;
    for (TagId id = 1; id <= 3; ++id) {
        Tag t;
        t.id = id;
        t.name = QStringLiteral("Tag%1").arg(id);
        t.address = quint16(id * 2);
        t.type = ValueType::Float32;
        p.tags.append(t);
    }
    p.nextTagId = 4;
    return p;
}

TagSample sample(TagId id, double v)
{
    TagSample s;
    s.tagId = id;
    s.value = v;
    s.quality = std::isnan(v) ? Quality::Timeout : Quality::Good;
    return s;
}

void addRow(Recorder &r, double t, std::initializer_list<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    b.samples = QList<TagSample>(samples);
    r.append(b);
}

QDateTime sampleStart()
{
    return QDateTime(QDate(2026, 10, 9), QTime(14, 3, 12, 250), QTimeZone::fromSecondsAheadOfUtc(-4 * 3600));
}

Recorder sampleRecorder()
{
    Recorder r(1000);
    r.setStartTime(sampleStart());
    addRow(r, 0.0, {sample(2, 1.5), sample(1, 10.0)});
    addRow(r, 0.1, {sample(2, kNan), sample(1, 11.0)});
    addRow(r, 0.1, {sample(2, 2.5), sample(1, kNan)});
    addRow(r, 0.35, {sample(2, -3.25), sample(1, 13.0), sample(3, 100.0)}); // tag 3 appears late
    addRow(r, 1e6 + 0.123456789, {sample(2, 1e-300), sample(1, 1e300), sample(3, kNan)});
    return r;
}

void compareRecorders(const Recorder &a, const Recorder &b)
{
    QCOMPARE(a.size(), b.size());
    QCOMPARE(a.tagIds(), b.tagIds());
    QCOMPARE(a.startTime().toMSecsSinceEpoch(), b.startTime().toMSecsSinceEpoch());
    QCOMPARE(a.startTime().isValid(), b.startTime().isValid());
    for (qsizetype i = 0; i < a.size(); ++i) {
        QCOMPARE(a.timeAt(i), b.timeAt(i));
        for (const TagId id : a.tagIds()) {
            const double x = a.valueAt(i, id);
            const double y = b.valueAt(i, id);
            if (std::isnan(x))
                QVERIFY(std::isnan(y));
            else
                QCOMPARE(x, y);
        }
    }
}

QJsonObject parse(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

} // namespace

class Test : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip();
    void emptyRecorder();
    void invalidStartTime();
    void columnForUnknownTag();
    void capacityRaisedNeverLowered();
    void failures_data();
    void failures();
    void fileSaveLoad();
    void loadMissingFile();
    void timing();
};

void Test::roundTrip()
{
    const Project proj = sampleProject();
    const Recorder rec = sampleRecorder();
    SessionCursors cur;
    cur.trendC1 = 12.5;
    cur.analysisC2 = -0.75;

    const QByteArray json = sessionToJson(proj, rec, cur);
    QVERIFY(!json.isEmpty());

    Project proj2;
    Recorder rec2(10);
    SessionCursors cur2;
    cur2.trendC2 = 99.0; // must be replaced (reset to unset)
    QString err;
    QVERIFY2(sessionFromJson(json, proj2, rec2, cur2, &err), qPrintable(err));
    QCOMPARE(proj2, proj);
    QCOMPARE(cur2, cur);
    QCOMPARE(rec2.tagIds(), (QList<TagId>{2, 1, 3}));
    compareRecorders(rec, rec2);
    QCOMPARE(rec2.startTime().offsetFromUtc(), -4 * 3600);
    QVERIFY(std::isnan(rec2.valueAt(0, 3))); // late tag: earlier rows are NaN
    QCOMPARE(rec2.valueAt(3, 3), 100.0);
}

void Test::emptyRecorder()
{
    Recorder rec(100);
    rec.setStartTime(sampleStart());
    const Project proj = sampleProject();
    const QByteArray json = sessionToJson(proj, rec, {});

    const QJsonObject r = parse(json).value("recording").toObject();
    QCOMPARE(r.value("rows").toInt(), 0);
    QVERIFY(r.value("columns").toArray().isEmpty());

    Project p2;
    Recorder rec2(100);
    addRow(rec2, 1.0, {sample(5, 1.0)});
    SessionCursors c2;
    QString err;
    QVERIFY2(sessionFromJson(json, p2, rec2, c2, &err), qPrintable(err));
    QCOMPARE(p2, proj);
    QCOMPARE(rec2.size(), qsizetype(0));
    QVERIFY(rec2.tagIds().isEmpty());
    QCOMPARE(rec2.startTime().toMSecsSinceEpoch(), sampleStart().toMSecsSinceEpoch());
    QCOMPARE(c2, SessionCursors{});
}

void Test::invalidStartTime()
{
    Recorder rec(100);
    addRow(rec, 0.0, {sample(1, 1.0)});
    QVERIFY(!rec.startTime().isValid());
    const QByteArray json = sessionToJson(sampleProject(), rec, {});
    QCOMPARE(parse(json).value("recording").toObject().value("startTime").toString(), QString());

    Project p;
    Recorder rec2(100);
    rec2.setStartTime(sampleStart());
    SessionCursors c;
    QVERIFY(sessionFromJson(json, p, rec2, c));
    QVERIFY(!rec2.startTime().isValid());
    QCOMPARE(rec2.size(), qsizetype(1));
}

void Test::columnForUnknownTag()
{
    Recorder rec(100);
    addRow(rec, 0.0, {sample(77, 5.0), sample(1, 1.0)});
    addRow(rec, 1.0, {sample(77, 6.0), sample(1, 2.0)});
    const QByteArray json = sessionToJson(sampleProject(), rec, {});
    Project p;
    Recorder rec2(100);
    SessionCursors c;
    QString err;
    QVERIFY2(sessionFromJson(json, p, rec2, c, &err), qPrintable(err));
    QCOMPARE(rec2.tagIds(), (QList<TagId>{77, 1}));
    QCOMPARE(rec2.valueAt(1, 77), 6.0);
    compareRecorders(rec, rec2);
}

void Test::capacityRaisedNeverLowered()
{
    Recorder rec(100);
    for (int i = 0; i < 50; ++i)
        addRow(rec, i * 0.1, {sample(1, i)});
    const QByteArray json = sessionToJson(sampleProject(), rec, {});

    Project p;
    SessionCursors c;
    Recorder small(10);
    QVERIFY(sessionFromJson(json, p, small, c));
    QCOMPARE(small.size(), qsizetype(50));
    QCOMPARE(small.capacity(), qsizetype(50 + 72000)); // room to continue for 2 h

    Recorder big(200000);
    QVERIFY(sessionFromJson(json, p, big, c));
    QCOMPARE(big.size(), qsizetype(50));
    QCOMPARE(big.capacity(), qsizetype(200000)); // never lowered
}

void Test::failures_data()
{
    QTest::addColumn<QByteArray>("json");

    const QByteArray good = sessionToJson(sampleProject(), sampleRecorder(), {});
    const QJsonObject goodObj = parse(good);

    const auto modified = [&](auto &&fn) {
        QJsonObject o = goodObj;
        fn(o);
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    };
    const auto withRecording = [&](auto &&fn) {
        return modified([&](QJsonObject &o) {
            QJsonObject rec = o.value("recording").toObject();
            fn(rec);
            o.insert("recording", rec);
        });
    };

    QTest::newRow("bad json") << QByteArray("{not json");
    QTest::newRow("not an object") << QByteArray("[1,2]");
    QTest::newRow("wrong format") << modified([](QJsonObject &o) { o.insert("format", "other"); });
    QTest::newRow("version 2") << modified([](QJsonObject &o) { o.insert("version", 2); });
    QTest::newRow("bad project") << modified([](QJsonObject &o) {
        QJsonObject p = o.value("project").toObject();
        p.insert("format", "nope");
        o.insert("project", p);
    });
    QTest::newRow("bad base64 times") << withRecording([](QJsonObject &r) {
        r.insert("times", "!!!not base64!!!");
    });
    QTest::newRow("bad compressed data") << withRecording([](QJsonObject &r) {
        r.insert("times", QString::fromLatin1(QByteArray("hello world, not zlib").toBase64()));
    });
    QTest::newRow("wrong decoded size") << withRecording([](QJsonObject &r) {
        r.insert("rows", 4);
    });
    QTest::newRow("negative rows") << withRecording([](QJsonObject &r) { r.insert("rows", -1); });
    QTest::newRow("decreasing times") << withRecording([](QJsonObject &r) {
        const double t[5] = {0.0, 2.0, 1.0, 3.0, 4.0};
        QByteArray raw(5 * 8, Qt::Uninitialized);
        for (int i = 0; i < 5; ++i)
            qToLittleEndian<quint64>(std::bit_cast<quint64>(t[i]), raw.data() + i * 8);
        r.insert("times", QString::fromLatin1(qCompress(raw).toBase64()));
    });
    QTest::newRow("duplicate column ids") << withRecording([](QJsonObject &r) {
        QJsonArray cols = r.value("columns").toArray();
        cols.append(cols.first());
        r.insert("columns", cols);
    });
    QTest::newRow("bad column values") << withRecording([](QJsonObject &r) {
        QJsonArray cols = r.value("columns").toArray();
        QJsonObject c0 = cols.first().toObject();
        c0.insert("values", "AAAA");
        cols[0] = c0;
        r.insert("columns", cols);
    });
    QTest::newRow("cursor value x") << modified([](QJsonObject &o) {
        QJsonObject trend;
        trend.insert("c1", "x");
        QJsonObject cur;
        cur.insert("trend", trend);
        o.insert("cursors", cur);
    });
    QTest::newRow("bad start time") << withRecording([](QJsonObject &r) {
        r.insert("startTime", "yesterday");
    });
}

void Test::failures()
{
    QFETCH(QByteArray, json);

    Project proj = sampleProject();
    proj.connection.host = QStringLiteral("untouched");
    Recorder rec = sampleRecorder();
    SessionCursors cur;
    cur.analysisC1 = 4.0;

    const Project projBefore = proj;
    const SessionCursors curBefore = cur;
    const Recorder recBefore = rec;

    QString err;
    QVERIFY(!sessionFromJson(json, proj, rec, cur, &err));
    QVERIFY(!err.isEmpty());
    QCOMPARE(proj, projBefore);
    QCOMPARE(cur, curBefore);
    QCOMPARE(rec.capacity(), recBefore.capacity());
    compareRecorders(rec, recBefore);

    QVERIFY(!sessionFromJson(json, proj, rec, cur)); // null error pointer is fine
}

void Test::fileSaveLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("a.mbsession");
    const Project proj = sampleProject();
    const Recorder rec = sampleRecorder();
    SessionCursors cur;
    cur.trendC1 = 1.0;
    cur.trendC2 = 2.0;
    cur.analysisC1 = 3.0;
    cur.analysisC2 = 4.0;

    QString err;
    QVERIFY2(saveSession(path, proj, rec, cur, &err), qPrintable(err));
    QVERIFY(QFileInfo::exists(path));

    Project p2;
    Recorder rec2(10);
    SessionCursors c2;
    QVERIFY2(loadSession(path, p2, rec2, c2, &err), qPrintable(err));
    QCOMPARE(p2, proj);
    QCOMPARE(c2, cur);
    compareRecorders(rec, rec2);

    // Saving into a missing directory fails with an error.
    err.clear();
    QVERIFY(!saveSession(dir.filePath("nope/x.mbsession"), proj, rec, cur, &err));
    QVERIFY(!err.isEmpty());
}

void Test::loadMissingFile()
{
    QTemporaryDir dir;
    Project p = sampleProject();
    const Project before = p;
    Recorder rec(10);
    SessionCursors c;
    QString err;
    QVERIFY(!loadSession(dir.filePath("missing.mbsession"), p, rec, c, &err));
    QVERIFY(!err.isEmpty());
    QCOMPARE(p, before);
}

void Test::timing()
{
    constexpr int kRows = 72000;
    constexpr int kTags = 20;
    Project proj = sampleProject();
    Recorder rec(kRows);
    rec.setStartTime(sampleStart());
    for (int r = 0; r < kRows; ++r) {
        SampleBatch b;
        b.t = r * 0.1;
        for (int k = 1; k <= kTags; ++k)
            b.samples.append(sample(TagId(k), (r % 97 == 0 && k % 3 == 0) ? kNan : std::sin(r * 0.001 * k)));
        rec.append(b);
    }
    QCOMPARE(rec.size(), qsizetype(kRows));

    QElapsedTimer timer;
    timer.start();
    const QByteArray json = sessionToJson(proj, rec, {});
    const qint64 saveMs = timer.restart();

    Project p2;
    Recorder rec2(100);
    SessionCursors c2;
    QString err;
    QVERIFY2(sessionFromJson(json, p2, rec2, c2, &err), qPrintable(err));
    const qint64 loadMs = timer.elapsed();
    qInfo().noquote() << QStringLiteral("72000 rows x 20 tags: save %1 ms, load %2 ms, %3 KiB")
                             .arg(saveMs).arg(loadMs).arg(json.size() / 1024);
#ifdef NDEBUG
    // Only in release builds: debug timings depend too much on machine load to be a pass/fail check.
    QVERIFY2(saveMs + loadMs < 5000, "too slow");
#endif
    compareRecorders(rec, rec2);
}

QTEST_MAIN(Test)
#include "tst_sessionio.moc"
