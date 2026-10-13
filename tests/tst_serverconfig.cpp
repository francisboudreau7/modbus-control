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
        {10, {SignalKind::Lag, 1000.0, 0.0, 5.0, 2.0, 100, 1.0}},
        {20, {SignalKind::Sine, 123.456, 0.0, 5.0, 2.0, 0}, RegisterFormat::Float32, WordOrder::ABCD},
        {22, {SignalKind::Sine, 123.456, 0.0, 5.0, 2.0, 0}, RegisterFormat::Float32, WordOrder::CDAB},
        {24, {SignalKind::Sawtooth, 100000.0, 0.0, 8.0, 2.0, 0}, RegisterFormat::Int32, WordOrder::ABCD},
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
    QCOMPARE(actual.params.deadTimeS, expected.params.deadTimeS);
    QCOMPARE(static_cast<int>(actual.format), static_cast<int>(expected.format));
    QCOMPARE(static_cast<int>(actual.byteOrder), static_cast<int>(expected.byteOrder));
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
    QCOMPARE(a.holdingValues.size(), b.holdingValues.size());
    for (qsizetype i = 0; i < a.holdingValues.size(); ++i) {
        const HoldingValue &x = a.holdingValues.at(i);
        const HoldingValue &y = b.holdingValues.at(i);
        QCOMPARE(x.address, y.address);
        QCOMPARE(x.value, y.value);
        QCOMPARE(static_cast<int>(x.format), static_cast<int>(y.format));
        QCOMPARE(static_cast<int>(x.byteOrder), static_cast<int>(y.byteOrder));
    }
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
    void formatAndByteOrder_data();
    void formatAndByteOrder();
    void signalKindStrings_data();
    void signalKindStrings();
    void acceptsBoundaryValues_data();
    void acceptsBoundaryValues();
    void rejects_data();
    void rejects();
    void loadMissingFileFails();
    void loadTemporaryFileWorks();
    void holdingKeysAndDefaults();
    void holdingFormats_data();
    void holdingFormats();
    void holdingBoundaryValues_data();
    void holdingBoundaryValues();
    void holdingRejects_data();
    void holdingRejects();
    void defaultHoldingDroppedWhenItDoesNotFit();
    void defaultHoldingEncoding();
    void unsignedEncoding();
    void strict32BitKey();
};

void TstServerConfig::defaultsMatchSpec()
{
    const ServerConfig cfg = ServerConfig::defaults();
    QCOMPARE(cfg.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(cfg.port, quint16(1502));
    QCOMPARE(cfg.unitId, 1);
    QCOMPARE(cfg.holdingCount, 200);
    QCOMPARE(cfg.inputCount, 64);
    QCOMPARE(cfg.tickMs, 50);
    QCOMPARE(cfg.delayMs, 0);
    QCOMPARE(cfg.dropPercent, 0.0);
    QVERIFY(!cfg.verbose);
    compareBindings(cfg.signalBindings, defaultBindings());
    QCOMPARE(cfg.signalBindings.size(), 10);

    const SignalBinding &lag = cfg.signalBindings.at(6);
    QCOMPARE(lag.inputAddress, quint16(10));
    QCOMPARE(lag.params.sourceAddress, quint16(100));
    QCOMPARE(lag.params.tauS, 2.0);
    QCOMPARE(lag.params.deadTimeS, 1.0);
    QCOMPARE(static_cast<int>(lag.format), static_cast<int>(RegisterFormat::Int16));

    // The 32-bit defaults: IR20-21 float32 ABCD, IR22-23 float32 CDAB, IR24-25 int32 ABCD.
    const SignalBinding &ir20 = cfg.signalBindings.at(7);
    QCOMPARE(ir20.inputAddress, quint16(20));
    QCOMPARE(static_cast<int>(ir20.format), static_cast<int>(RegisterFormat::Float32));
    QCOMPARE(static_cast<int>(ir20.byteOrder), static_cast<int>(WordOrder::ABCD));
    const SignalBinding &ir22 = cfg.signalBindings.at(8);
    QCOMPARE(ir22.inputAddress, quint16(22));
    QCOMPARE(static_cast<int>(ir22.format), static_cast<int>(RegisterFormat::Float32));
    QCOMPARE(static_cast<int>(ir22.byteOrder), static_cast<int>(WordOrder::CDAB));
    const SignalBinding &ir24 = cfg.signalBindings.at(9);
    QCOMPARE(ir24.inputAddress, quint16(24));
    QCOMPARE(static_cast<int>(ir24.format), static_cast<int>(RegisterFormat::Int32));
    QCOMPARE(static_cast<int>(ir24.byteOrder), static_cast<int>(WordOrder::ABCD));

    // The CM50-like default holding values: HR110-111 float32 CDAB 52.3, HR120-121 int32 ABCD 100.
    QCOMPARE(cfg.holdingValues.size(), 2);
    const HoldingValue &setpoint = cfg.holdingValues.at(0);
    QCOMPARE(setpoint.address, quint16(110));
    QCOMPARE(setpoint.value, 52.3);
    QCOMPARE(static_cast<int>(setpoint.format), static_cast<int>(RegisterFormat::Float32));
    QCOMPARE(static_cast<int>(setpoint.byteOrder), static_cast<int>(WordOrder::CDAB));
    const HoldingValue &int32Value = cfg.holdingValues.at(1);
    QCOMPARE(int32Value.address, quint16(120));
    QCOMPARE(int32Value.value, 100.0);
    QCOMPARE(static_cast<int>(int32Value.format), static_cast<int>(RegisterFormat::Int32));
    QCOMPARE(static_cast<int>(int32Value.byteOrder), static_cast<int>(WordOrder::ABCD));
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
        R"JSON({"signals":[{"input":12,"kind":"lag","source":42,"tauS":3.5,"deadTimeS":2.5,"offset":7}]})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    compareBindings(cfg->signalBindings,
                    {{12, {SignalKind::Lag, 1000.0, 7.0, 5.0, 3.5, 42, 2.5}}});
}

void TstServerConfig::formatAndByteOrder_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<int>("format");
    QTest::addColumn<int>("order");
    const int int16 = static_cast<int>(RegisterFormat::Int16);
    const int int32 = static_cast<int>(RegisterFormat::Int32);
    const int float32 = static_cast<int>(RegisterFormat::Float32);
    const int abcd = static_cast<int>(WordOrder::ABCD);
    const int cdab = static_cast<int>(WordOrder::CDAB);
    const int badc = static_cast<int>(WordOrder::BADC);
    const int dcba = static_cast<int>(WordOrder::DCBA);
    QTest::newRow("defaults") << QByteArray(R"({"signals":[{"input":4,"kind":"sine"}]})") << int16 << abcd;
    QTest::newRow("int16 CDAB") << QByteArray(R"({"signals":[{"input":4,"kind":"sine","format":"int16","byteOrder":"CDAB"}]})")
                                << int16 << cdab;
    QTest::newRow("int32 BADC") << QByteArray(R"({"signals":[{"input":4,"kind":"sine","format":"int32","byteOrder":"BADC"}]})")
                                << int32 << badc;
    QTest::newRow("float32 DCBA") << QByteArray(R"({"signals":[{"input":4,"kind":"sine","format":"float32","byteOrder":"DCBA"}]})")
                                  << float32 << dcba;
    QTest::newRow("float32 default order") << QByteArray(R"({"signals":[{"input":4,"kind":"sine","format":"float32"}]})")
                                           << float32 << abcd;
    QTest::newRow("byteOrder without format") << QByteArray(R"({"signals":[{"input":4,"kind":"counter","byteOrder":"DCBA"}]})")
                                              << int16 << dcba;
}

void TstServerConfig::formatAndByteOrder()
{
    QFETCH(QByteArray, json);
    QFETCH(int, format);
    QFETCH(int, order);
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QCOMPARE(cfg->signalBindings.size(), 1);
    QCOMPARE(static_cast<int>(cfg->signalBindings.at(0).format), format);
    QCOMPARE(static_cast<int>(cfg->signalBindings.at(0).byteOrder), order);
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
    QTest::newRow("deadTimeS 0")
        << QByteArray(R"({"signals":[{"input":10,"kind":"lag","source":1,"deadTimeS":0}]})");
    QTest::newRow("deadTimeS 60")
        << QByteArray(R"({"signals":[{"input":10,"kind":"lag","source":1,"deadTimeS":60}]})");
    QTest::newRow("small period") << QByteArray(R"({"signals":[{"kind":"sine","periodS":0.001}]})");
    QTest::newRow("float32 ending at inputCount-1")
        << QByteArray(R"({"inputCount":8,"signals":[{"input":6,"kind":"sine","format":"float32"}]})");
    QTest::newRow("int32 ending at inputCount-1")
        << QByteArray(R"({"inputCount":8,"signals":[{"input":6,"kind":"counter","format":"int32","byteOrder":"DCBA"}]})");
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
    QTest::newRow("unknown format") << QByteArray(R"({"signals":[{"kind":"sine","format":"int64"}]})")
                                    << QStringLiteral("format");
    QTest::newRow("format is case-sensitive") << QByteArray(R"({"signals":[{"kind":"sine","format":"Float32"}]})")
                                              << QStringLiteral("format");
    QTest::newRow("format not a string") << QByteArray(R"({"signals":[{"kind":"sine","format":32}]})")
                                         << QStringLiteral("format");
    QTest::newRow("unknown byteOrder") << QByteArray(R"({"signals":[{"kind":"sine","format":"int32","byteOrder":"ABC"}]})")
                                       << QStringLiteral("byteOrder");
    QTest::newRow("byteOrder is case-sensitive") << QByteArray(R"({"signals":[{"kind":"sine","byteOrder":"abcd"}]})")
                                                 << QStringLiteral("byteOrder");
    QTest::newRow("float32 at inputCount-1")
        << QByteArray(R"({"inputCount":8,"signals":[{"input":7,"kind":"sine","format":"float32"}]})")
        << QStringLiteral("signals[0].input");
    QTest::newRow("int32 at inputCount-1")
        << QByteArray(R"({"inputCount":8,"signals":[{"input":7,"kind":"counter","format":"int32","byteOrder":"CDAB"}]})")
        << QStringLiteral("signals[0].input");
    QTest::newRow("float32 past 65535")
        << QByteArray(R"({"signals":[{"input":65535,"kind":"sine","format":"float32"}]})")
        << QStringLiteral("signals[0].input");
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
    QTest::newRow("deadTimeS -1") << QByteArray(R"({"signals":[{"kind":"lag","source":1,"deadTimeS":-1}]})")
                                  << QStringLiteral("signals[0].deadTimeS");
    QTest::newRow("deadTimeS 61") << QByteArray(R"({"signals":[{"kind":"lag","source":1,"deadTimeS":61}]})")
                                  << QStringLiteral("signals[0].deadTimeS");
    QTest::newRow("deadTimeS as string")
        << QByteArray(R"({"signals":[{"kind":"lag","source":1,"deadTimeS":"1"}]})")
        << QStringLiteral("signals[0].deadTimeS");
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

void TstServerConfig::holdingKeysAndDefaults()
{
    QString error;
    const auto cfg = parseServerConfig(R"JSON({"holding":[{"address":5,"value":7}]})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    // "holding" replaces the default initial values; format and byteOrder default to int16 ABCD.
    QCOMPARE(cfg->holdingValues.size(), 1);
    const HoldingValue &entry = cfg->holdingValues.at(0);
    QCOMPARE(entry.address, quint16(5));
    QCOMPARE(entry.value, 7.0);
    QCOMPARE(static_cast<int>(entry.format), static_cast<int>(RegisterFormat::Int16));
    QCOMPARE(static_cast<int>(entry.byteOrder), static_cast<int>(WordOrder::ABCD));

    const auto empty = parseServerConfig(R"JSON({"holding":[]})JSON", &error);
    QVERIFY2(empty.has_value(), qPrintable(error));
    QVERIFY(empty->holdingValues.isEmpty());
}

void TstServerConfig::holdingFormats_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<int>("format");
    QTest::addColumn<int>("order");
    QTest::addColumn<double>("value");
    const int int16 = static_cast<int>(RegisterFormat::Int16);
    const int uint16 = static_cast<int>(RegisterFormat::UInt16);
    const int int32 = static_cast<int>(RegisterFormat::Int32);
    const int uint32 = static_cast<int>(RegisterFormat::UInt32);
    const int float32 = static_cast<int>(RegisterFormat::Float32);
    const int abcd = static_cast<int>(WordOrder::ABCD);
    const int cdab = static_cast<int>(WordOrder::CDAB);
    const int badc = static_cast<int>(WordOrder::BADC);
    const int dcba = static_cast<int>(WordOrder::DCBA);
    QTest::newRow("int16 negative") << QByteArray(R"({"holding":[{"address":0,"value":-5}]})") << int16 << abcd << -5.0;
    QTest::newRow("uint16 max") << QByteArray(R"({"holding":[{"address":0,"value":65535,"format":"uint16"}]})")
                                << uint16 << abcd << 65535.0;
    QTest::newRow("int32 CDAB") << QByteArray(R"({"holding":[{"address":0,"value":-100,"format":"int32","byteOrder":"CDAB"}]})")
                                << int32 << cdab << -100.0;
    QTest::newRow("uint32 BADC") << QByteArray(R"({"holding":[{"address":0,"value":3000000000,"format":"uint32","byteOrder":"BADC"}]})")
                                 << uint32 << badc << 3000000000.0;
    QTest::newRow("float32 DCBA") << QByteArray(R"({"holding":[{"address":0,"value":52.3,"format":"float32","byteOrder":"DCBA"}]})")
                                  << float32 << dcba << 52.3;
    QTest::newRow("16-bit ignores byteOrder") << QByteArray(R"({"holding":[{"address":0,"value":1,"byteOrder":"DCBA"}]})")
                                              << int16 << dcba << 1.0;
}

void TstServerConfig::holdingFormats()
{
    QFETCH(QByteArray, json);
    QFETCH(int, format);
    QFETCH(int, order);
    QFETCH(double, value);
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QCOMPARE(cfg->holdingValues.size(), 1);
    QCOMPARE(static_cast<int>(cfg->holdingValues.at(0).format), format);
    QCOMPARE(static_cast<int>(cfg->holdingValues.at(0).byteOrder), order);
    QCOMPARE(cfg->holdingValues.at(0).value, value);
}

void TstServerConfig::holdingBoundaryValues_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::newRow("int16 min") << QByteArray(R"({"holding":[{"address":0,"value":-32768}]})");
    QTest::newRow("int16 max") << QByteArray(R"({"holding":[{"address":0,"value":32767}]})");
    QTest::newRow("uint16 max")
        << QByteArray(R"({"holdingCount":65536,"holding":[{"address":65535,"value":65535,"format":"uint16"}]})");
    QTest::newRow("int32 min") << QByteArray(R"({"holding":[{"address":0,"value":-2147483648,"format":"int32"}]})");
    QTest::newRow("uint32 max") << QByteArray(R"({"holding":[{"address":0,"value":4294967295,"format":"uint32"}]})");
    QTest::newRow("float32 max") << QByteArray(R"({"holding":[{"address":0,"value":3.4028234663852886e38,"format":"float32"}]})");
    QTest::newRow("32-bit ending at holdingCount-1")
        << QByteArray(R"({"holdingCount":3,"signals":[],"holding":[{"address":1,"value":1,"format":"int32"}]})");
    QTest::newRow("adjacent 32-bit entries")
        << QByteArray(R"({"holding":[{"address":0,"value":1,"format":"int32"},{"address":2,"value":2}]})");
}

void TstServerConfig::holdingBoundaryValues()
{
    QFETCH(QByteArray, json);
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
}

void TstServerConfig::holdingRejects_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("key");
    QTest::newRow("holding not an array") << QByteArray(R"({"holding":{}})") << QStringLiteral("holding");
    QTest::newRow("entry not an object") << QByteArray(R"({"holding":[5]})") << QStringLiteral("holding[0]");
    QTest::newRow("address missing") << QByteArray(R"({"holding":[{"value":1}]})") << QStringLiteral("holding[0].address");
    QTest::newRow("value missing") << QByteArray(R"({"holding":[{"address":1}]})") << QStringLiteral("holding[0].value");
    QTest::newRow("address fractional") << QByteArray(R"({"holding":[{"address":1.5,"value":1}]})")
                                        << QStringLiteral("holding[0].address");
    QTest::newRow("address negative") << QByteArray(R"({"holding":[{"address":-1,"value":1}]})")
                                      << QStringLiteral("holding[0].address");
    QTest::newRow("address past holdingCount") << QByteArray(R"({"holding":[{"address":200,"value":1}]})")
                                               << QStringLiteral("holding[0].address");
    QTest::newRow("32-bit past holdingCount")
        << QByteArray(R"({"holding":[{"address":199,"value":1,"format":"int32"}]})") << QStringLiteral("holding[0].address");
    QTest::newRow("value as string") << QByteArray(R"({"holding":[{"address":1,"value":"1"}]})")
                                     << QStringLiteral("holding[0].value");
    QTest::newRow("value null") << QByteArray(R"({"holding":[{"address":1,"value":null}]})")
                                << QStringLiteral("holding[0].value");
    QTest::newRow("int16 too large") << QByteArray(R"({"holding":[{"address":1,"value":32768}]})")
                                     << QStringLiteral("holding[0].value");
    QTest::newRow("uint16 negative") << QByteArray(R"({"holding":[{"address":1,"value":-1,"format":"uint16"}]})")
                                     << QStringLiteral("holding[0].value");
    QTest::newRow("int16 fraction") << QByteArray(R"({"holding":[{"address":1,"value":1.5}]})")
                                    << QStringLiteral("holding[0].value");
    QTest::newRow("int32 too large") << QByteArray(R"({"holding":[{"address":1,"value":2147483648,"format":"int32"}]})")
                                     << QStringLiteral("holding[0].value");
    QTest::newRow("uint32 too large") << QByteArray(R"({"holding":[{"address":1,"value":4294967296,"format":"uint32"}]})")
                                      << QStringLiteral("holding[0].value");
    QTest::newRow("int32 fraction") << QByteArray(R"({"holding":[{"address":1,"value":1.5,"format":"int32"}]})")
                                    << QStringLiteral("holding[0].value");
    QTest::newRow("float32 too large") << QByteArray(R"({"holding":[{"address":1,"value":1e39,"format":"float32"}]})")
                                       << QStringLiteral("holding[0].value");
    QTest::newRow("unknown format") << QByteArray(R"({"holding":[{"address":1,"value":1,"format":"int64"}]})")
                                    << QStringLiteral("holding[0].format");
    QTest::newRow("format is case-sensitive") << QByteArray(R"({"holding":[{"address":1,"value":1,"format":"Int16"}]})")
                                              << QStringLiteral("holding[0].format");
    QTest::newRow("unsigned format not accepted for signals")
        << QByteArray(R"({"signals":[{"kind":"sine","format":"uint16"}]})") << QStringLiteral("format");
    QTest::newRow("unknown byteOrder") << QByteArray(R"({"holding":[{"address":1,"value":1,"format":"int32","byteOrder":"ABC"}]})")
                                       << QStringLiteral("holding[0].byteOrder");
    QTest::newRow("unknown key") << QByteArray(R"({"holding":[{"address":1,"value":1,"kind":"sine"}]})")
                                 << QStringLiteral("holding[0].kind");
    QTest::newRow("32-bit overlaps next entry")
        << QByteArray(R"({"holding":[{"address":5,"value":1,"format":"int32"},{"address":6,"value":2}]})")
        << QStringLiteral("overlaps");
    QTest::newRow("same address twice") << QByteArray(R"({"holding":[{"address":5,"value":1},{"address":5,"value":2}]})")
                                        << QStringLiteral("overlaps");
    QTest::newRow("explicit entry does not fit holdingCount")
        << QByteArray(R"({"holdingCount":100,"signals":[],"holding":[{"address":110,"value":1}]})")
        << QStringLiteral("holding[0].address");
}

void TstServerConfig::holdingRejects()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, key);
    QString error;
    const auto cfg = parseServerConfig(json, &error);
    QVERIFY(!cfg.has_value());
    QVERIFY2(error.contains(key), qPrintable(error));
}

void TstServerConfig::defaultHoldingDroppedWhenItDoesNotFit()
{
    // Without a "holding" key, default initial values that do not fit below holdingCount are
    // dropped, so files written before the key existed still load.
    QString error;
    auto cfg = parseServerConfig(R"JSON({"holdingCount":101})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QVERIFY(cfg->holdingValues.isEmpty());

    cfg = parseServerConfig(R"JSON({"holdingCount":112})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QCOMPARE(cfg->holdingValues.size(), 1);
    QCOMPARE(cfg->holdingValues.at(0).address, quint16(110));

    cfg = parseServerConfig(R"JSON({"holdingCount":122})JSON", &error);
    QVERIFY2(cfg.has_value(), qPrintable(error));
    QCOMPARE(cfg->holdingValues.size(), 2);
}

void TstServerConfig::defaultHoldingEncoding()
{
    // ServerLoop::start writes each entry with encodeRegisters(); check the default entries.
    const ServerConfig cfg = ServerConfig::defaults();
    QCOMPARE(cfg.holdingValues.size(), 2);
    quint16 regs[2] = {0, 0};
    const HoldingValue &setpoint = cfg.holdingValues.at(0);
    encodeRegisters(setpoint.value, setpoint.format, setpoint.byteOrder, regs);
    QCOMPARE(regs[0], quint16(0x3333)); // HR110
    QCOMPARE(regs[1], quint16(0x4251)); // HR111
    const HoldingValue &int32Value = cfg.holdingValues.at(1);
    encodeRegisters(int32Value.value, int32Value.format, int32Value.byteOrder, regs);
    QCOMPARE(regs[0], quint16(0x0000)); // HR120
    QCOMPARE(regs[1], quint16(0x0064)); // HR121
}

void TstServerConfig::unsignedEncoding()
{
    quint16 regs[2] = {0, 0};
    encodeRegisters(65535.0, RegisterFormat::UInt16, WordOrder::ABCD, regs);
    QCOMPARE(regs[0], quint16(0xFFFF));
    encodeRegisters(70000.0, RegisterFormat::UInt16, WordOrder::ABCD, regs);
    QCOMPARE(regs[0], quint16(0xFFFF));
    encodeRegisters(-5.0, RegisterFormat::UInt16, WordOrder::ABCD, regs);
    QCOMPARE(regs[0], quint16(0));
    encodeRegisters(305419896.0, RegisterFormat::UInt32, WordOrder::ABCD, regs);
    QCOMPARE(regs[0], quint16(0x1234));
    QCOMPARE(regs[1], quint16(0x5678));
    encodeRegisters(4294967296.0, RegisterFormat::UInt32, WordOrder::ABCD, regs);
    QCOMPARE(regs[0], quint16(0xFFFF));
    QCOMPARE(regs[1], quint16(0xFFFF));
    encodeRegisters(-1.0, RegisterFormat::UInt32, WordOrder::CDAB, regs);
    QCOMPARE(regs[0], quint16(0));
    QCOMPARE(regs[1], quint16(0));
}

void TstServerConfig::strict32BitKey()
{
    QVERIFY(!ServerConfig::defaults().strict32Bit);
    QString error;
    const auto empty = parseServerConfig("{}", &error);
    QVERIFY2(empty.has_value(), qPrintable(error));
    QVERIFY(!empty->strict32Bit);

    const auto on = parseServerConfig(R"JSON({"strict32Bit": true})JSON", &error);
    QVERIFY2(on.has_value(), qPrintable(error));
    QVERIFY(on->strict32Bit);

    const auto off = parseServerConfig(R"JSON({"strict32Bit": false})JSON", &error);
    QVERIFY2(off.has_value(), qPrintable(error));
    QVERIFY(!off->strict32Bit);

    for (const char *json : {R"JSON({"strict32Bit": 1})JSON", R"JSON({"strict32Bit": "true"})JSON"}) {
        const auto bad = parseServerConfig(json, &error);
        QVERIFY(!bad.has_value());
        QVERIFY2(error.contains(QStringLiteral("strict32Bit")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("must be true or false")), qPrintable(error));
    }
}

QTEST_GUILESS_MAIN(TstServerConfig)
#include "tst_serverconfig.moc"
