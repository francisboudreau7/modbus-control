#include "ServerConfig.h"

#include <QtTest>
#include <QTemporaryFile>

using namespace mbsim;

namespace {

QList<SignalBinding> defaultBindings()
{
    return {
        {0, {SignalKind::Sine, 1000.0, 0.0, 5.0, 2.0, 0}},
        {1, {SignalKind::Triangle, 1000.0, 0.0, 10.0, 2.0, 0}},
        {2, {SignalKind::Counter, 1000.0, 0.0, 5.0, 2.0, 0}},
        {3, {SignalKind::Square, 1000.0, 0.0, 4.0, 2.0, 0}},
        {4, {SignalKind::Noise, 100.0, 0.0, 5.0, 2.0, 0}},
        {5, {SignalKind::Sawtooth, 1000.0, 0.0, 8.0, 2.0, 0}},
        {10, {SignalKind::Lag, 1000.0, 0.0, 5.0, 2.0, 100}},
    };
}

void compareBinding(const SignalBinding &actual, const SignalBinding &expected)
{
    QCOMPARE(actual.inputAddress, expected.inputAddress);
    QCOMPARE(static_cast<int>(actual.params.kind), static_cast<int>(expected.params.kind));
    QCOMPARE(actual.params.amplitude, expected.params.amplitude);
    QCOMPARE(actual.params.offset, expected.params.offset);
    QCOMPARE(actual.params.periodS, expected.params.periodS);
    QCOMPARE(actual.params.tauS, expected.params.tauS);
    QCOMPARE(actual.params.sourceAddress, expected.params.sourceAddress);
}

void compareBindings(const QList<SignalBinding> &actual, const QList<SignalBinding> &expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (qsizetype i = 0; i < expected.size(); ++i)
        compareBinding(actual.at(i), expected.at(i));
}

void compareConfigs(const ServerConfig &a, const ServerConfig &b)
{
    QCOMPARE(a.host, b.host);
    QCOMPARE(a.port, b.port);
    QCOMPARE(a.unitId, b.unitId);
    QCOMPARE(a.holdingCount, b.holdingCount);
    QCOMPARE(a.inputCount, b.inputCount);
    QCOMPARE(a.tickMs, b.tickMs);
    QCOMPARE(a.delayMs, b.delayMs);
    QCOMPARE(a.dropPercent, b.dropPercent);
    QCOMPARE(a.verbose, b.verbose);
    compareBindings(a.signalBindings, b.signalBindings);
}

} // namespace

class TstServerConfig : public QObject
{
    Q_OBJECT
private slots:
    void defaultsMatchSpec();
    void emptyObjectGivesDefaults();
    void fullOverrideOfScalarKeys();
    void signalsArrayReplacesDefaults();
    void signalsEmptyArrayRemovesAll();
    void signalPerKeyDefaults();
    void lagBindingKeys();
    void signalKindStrings_data();
    void signalKindStrings();
    void acceptsBoundaryValues_data();
    void acceptsBoundaryValues();
    void rejects_data();
    void rejects();
    void loadMissingFileFails();
    void loadTemporaryFileWorks();
};

void TstServerConfig::defaultsMatchSpec()
{
    const ServerConfig cfg = ServerConfig::defaults();
    QCOMPARE(cfg.host, QStringLiteral("0.0.0.0"));
    QCOMPARE(cfg.port, quint16(1502));
    QCOMPARE(cfg.unitId, 1);
    QCOMPARE(cfg.holdingCount, 200);
    QCOMPARE(cfg.inputCount, 64);
    QCOMPARE(cfg.tickMs, 50);
    QCOMPARE(cfg.delayMs, 0);
    QCOMPARE(cfg.dropPercent, 0.0);
    QVERIFY(!cfg.verbose);
    compareBindings(cfg.signalBindings, defaultBindings());
    QCOMPARE(cfg.signalBindings.size(), 7);

    const SignalBinding &lag = cfg.signalBindings.constLast();
    QCOMPARE(lag.inputAddress, quint16(10));
    QCOMPARE(lag.params.sourceAddress, quint16(100));
    QCOMPARE(lag.params.tauS, 2.0);
}

void TstServerConfig::emptyObjectGivesDefaults()
{
    QString error;
    const auto cfg = parseServerConfig("{}", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    compareConfigs(*cfg, ServerConfig::defaults());
}

void TstServerConfig::fullOverrideOfScalarKeys()
{
    const QByteArray json = R"JSON({
        "host": "127.0.0.1", "port": 2502, "unitId": 7,
        "holdingCount": 150, "inputCount": 32, "tickMs": 20,
        "delayMs": 15, "dropPercent": 2.5, "verbose": true
    })JSON";
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QCOMPARE(cfg->host, QStringLiteral("127.0.0.1"));
    QCOMPARE(cfg->port, quint16(2502));
    QCOMPARE(cfg->unitId, 7);
    QCOMPARE(cfg->holdingCount, 150);
    QCOMPARE(cfg->inputCount, 32);
    QCOMPARE(cfg->tickMs, 20);
    QCOMPARE(cfg->delayMs, 15);
    QCOMPARE(cfg->dropPercent, 2.5);
    QVERIFY(cfg->verbose);
    // No "signals" key, so the default bindings stay.
    compareBindings(cfg->signalBindings, defaultBindings());
}

void TstServerConfig::signalsArrayReplacesDefaults()
{
    const QByteArray json = R"JSON({"signals":[
        {"input":3,"kind":"square","amplitude":500,"offset":10,"periodS":2}
    ]})JSON";
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    compareBindings(cfg->signalBindings,
                    {{3, {SignalKind::Square, 500.0, 10.0, 2.0, 2.0, 0}}});
}

void TstServerConfig::signalsEmptyArrayRemovesAll()
{
    QString error;
    const auto cfg = parseServerConfig(R"JSON({"signals":[]})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QVERIFY(cfg->signalBindings.isEmpty());
}

void TstServerConfig::signalPerKeyDefaults()
{
    QString error;
    const auto cfg = parseServerConfig(R"JSON({"signals":[{"kind":"noise"}]})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    compareBindings(cfg->signalBindings,
                    {{0, {SignalKind::Noise, 1000.0, 0.0, 5.0, 2.0, 0}}});
}

void TstServerConfig::lagBindingKeys()
{
    QString error;
    const auto cfg = parseServerConfig(
        R"JSON({"signals":[{"input":12,"kind":"lag","source":42,"tauS":3.5,"offset":7}]})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    compareBindings(cfg->signalBindings,
                    {{12, {SignalKind::Lag, 1000.0, 7.0, 5.0, 3.5, 42}}});
}

void TstServerConfig::signalKindStrings_data()
{
    QTest::addColumn<QString>("kindName");
    QTest::addColumn<int>("expected");
    QTest::newRow("constant") << QStringLiteral("constant") << static_cast<int>(SignalKind::Constant);
    QTest::newRow("sine") << QStringLiteral("sine") << static_cast<int>(SignalKind::Sine);
    QTest::newRow("triangle") << QStringLiteral("triangle") << static_cast<int>(SignalKind::Triangle);
    QTest::newRow("sawtooth") << QStringLiteral("sawtooth") << static_cast<int>(SignalKind::Sawtooth);
    QTest::newRow("square") << QStringLiteral("square") << static_cast<int>(SignalKind::Square);
    QTest::newRow("counter") << QStringLiteral("counter") << static_cast<int>(SignalKind::Counter);
    QTest::newRow("noise") << QStringLiteral("noise") << static_cast<int>(SignalKind::Noise);
    QTest::newRow("lag") << QStringLiteral("lag") << static_cast<int>(SignalKind::Lag);
}

void TstServerConfig::signalKindStrings()
{
    QFETCH(QString, kindName);
    QFETCH(int, expected);

    const QByteArray json = QStringLiteral(R"JSON({"signals":[{"input":0,"kind":"%1"}]})JSON")
                                .arg(kindName)
                                .toUtf8();
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QCOMPARE(cfg->signalBindings.size(), 1);
    QCOMPARE(static_cast<int>(cfg->signalBindings.at(0).params.kind), expected);
}

void TstServerConfig::acceptsBoundaryValues_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("port 65535") << QByteArray(R"({"port":65535})");
    QTest::newRow("counts at 65536") << QByteArray(R"({"holdingCount":65536,"inputCount":65536})");
    QTest::newRow("counts at 1") << QByteArray(R"({"holdingCount":1,"inputCount":1,"signals":[]})");
    QTest::newRow("dropPercent 100") << QByteArray(R"({"dropPercent":100})");
    QTest::newRow("tickMs 1") << QByteArray(R"({"tickMs":1})");
    QTest::newRow("unitId 0") << QByteArray(R"({"unitId":0})");
    QTest::newRow("unitId 247") << QByteArray(R"({"unitId":247})");
    QTest::newRow("input at inputCount-1")
        << QByteArray(R"({"inputCount":8,"signals":[{"input":7,"kind":"counter"}]})");
    QTest::newRow("lag source at holdingCount-1")
        << QByteArray(R"({"holdingCount":51,"signals":[{"input":10,"kind":"lag","source":50}]})");
    QTest::newRow("small period") << QByteArray(R"({"signals":[{"kind":"sine","periodS":0.001}]})");
}

void TstServerConfig::acceptsBoundaryValues()
{
    QFETCH(QByteArray, json);
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
}

void TstServerConfig::rejects_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("key");
    QTest::newRow("invalid JSON") << QByteArray("{") << QStringLiteral("Invalid JSON");
    QTest::newRow("top-level array") << QByteArray("[]") << QStringLiteral("object");
    QTest::newRow("port as string") << QByteArray(R"({"port":"1502"})") << QStringLiteral("port");
    QTest::newRow("port 0") << QByteArray(R"({"port":0})") << QStringLiteral("port");
    QTest::newRow("port 65536") << QByteArray(R"({"port":65536})") << QStringLiteral("port");
    QTest::newRow("port fractional") << QByteArray(R"({"port":1.5})") << QStringLiteral("port");
    QTest::newRow("holdingCount 0") << QByteArray(R"({"holdingCount":0})") << QStringLiteral("holdingCount");
    QTest::newRow("inputCount 65537") << QByteArray(R"({"inputCount":65537})") << QStringLiteral("inputCount");
    QTest::newRow("unitId 248") << QByteArray(R"({"unitId":248})") << QStringLiteral("unitId");
    QTest::newRow("unitId 255") << QByteArray(R"({"unitId":255})") << QStringLiteral("unitId");
    QTest::newRow("unitId -1") << QByteArray(R"({"unitId":-1})") << QStringLiteral("unitId");
    QTest::newRow("tickMs 0") << QByteArray(R"({"tickMs":0})") << QStringLiteral("tickMs");
    QTest::newRow("delayMs negative") << QByteArray(R"({"delayMs":-1})") << QStringLiteral("delayMs");
    QTest::newRow("dropPercent 150") << QByteArray(R"({"dropPercent":150})") << QStringLiteral("dropPercent");
    QTest::newRow("dropPercent -1") << QByteArray(R"({"dropPercent":-1})") << QStringLiteral("dropPercent");
    QTest::newRow("verbose as string") << QByteArray(R"({"verbose":"yes"})") << QStringLiteral("verbose");
    QTest::newRow("empty host") << QByteArray(R"({"host":""})") << QStringLiteral("host");
    QTest::newRow("signals not an array") << QByteArray(R"({"signals":{}})") << QStringLiteral("signals");
    QTest::newRow("signal not an object") << QByteArray(R"({"signals":[5]})") << QStringLiteral("signals[0]");
    QTest::newRow("unknown kind") << QByteArray(R"({"signals":[{"kind":"wobble"}]})") << QStringLiteral("kind");
    QTest::newRow("kind is case-sensitive") << QByteArray(R"({"signals":[{"kind":"Sine"}]})") << QStringLiteral("kind");
    QTest::newRow("kind not a string") << QByteArray(R"({"signals":[{"kind":1}]})") << QStringLiteral("kind");
    QTest::newRow("negative input") << QByteArray(R"({"signals":[{"input":-1,"kind":"sine"}]})")
                                    << QStringLiteral("signals[0].input");
    QTest::newRow("input >= inputCount")
        << QByteArray(R"({"inputCount":8,"signals":[{"input":8,"kind":"counter"}]})")
        << QStringLiteral("signals[0].input");
    QTest::newRow("lag source >= holdingCount")
        << QByteArray(R"({"holdingCount":50,"signals":[{"input":10,"kind":"lag","source":50}]})")
        << QStringLiteral("signals[0].source");
    QTest::newRow("periodS 0") << QByteArray(R"({"signals":[{"kind":"sine","periodS":0}]})")
                               << QStringLiteral("signals[0].periodS");
    QTest::newRow("periodS as string") << QByteArray(R"({"signals":[{"kind":"sine","periodS":"5"}]})")
                                       << QStringLiteral("periodS");
    QTest::newRow("tauS -1") << QByteArray(R"({"signals":[{"kind":"lag","source":1,"tauS":-1}]})")
                             << QStringLiteral("signals[0].tauS");
}

void TstServerConfig::rejects()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, key);
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY(!cfg.has_value());
    QVERIFY2(error.contains(key), qPrintable(error));
}

void TstServerConfig::loadMissingFileFails()
{
    QString error;
    const auto cfg = loadServerConfig(QStringLiteral("no-such-dir/missing-server.json"), &error);
    QVERIFY(!cfg.has_value());
    QVERIFY(!error.isEmpty());
}

void TstServerConfig::loadTemporaryFileWorks()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QVERIFY(file.write(R"({"port":2000,"holdingCount":120})") > 0);
    file.flush();
    file.close();

    QString error;
    const auto cfg = loadServerConfig(file.fileName(), &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QCOMPARE(cfg->port, quint16(2000));
    QCOMPARE(cfg->holdingCount, 120);
}

QTEST_GUILESS_MAIN(TstServerConfig)
#include "tst_serverconfig.moc"
