#include "ProjectIO.h"

#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace mb;

Q_DECLARE_METATYPE(ValueType)
Q_DECLARE_METATYPE(ByteOrder)

namespace {

Project sampleProject()
{
    Project p;
    p.connection.host = QStringLiteral("10.0.0.5");
    p.connection.port = 1502;
    p.connection.unitId = 7;
    p.connection.timeoutMs = 2500;
    p.connection.pollPeriodMs = 200;

    Tag temp;
    temp.id = 1;
    temp.name = QStringLiteral("Temp");
    temp.area = Area::HoldingRegister;
    temp.address = 100;
    temp.type = ValueType::Int16;
    temp.gain = 0.1;
    temp.offset = -40.0;
    temp.unit = QStringLiteral("\u00b0C");
    temp.color = QStringLiteral("#ff0000");
    temp.plotted = false;
    temp.writable = true;

    Tag pressure;
    pressure.id = 2;
    pressure.name = QStringLiteral("Pressure");
    pressure.area = Area::InputRegister;
    pressure.address = 3;
    pressure.type = ValueType::UInt16;
    pressure.gain = 2.5;
    pressure.offset = 1.25;
    pressure.unit = QStringLiteral("bar");
    pressure.color = QStringLiteral("#00ff00");
    pressure.plotted = true;
    pressure.writable = false;

    Tag unicodeName;
    unicodeName.id = 5;
    unicodeName.name = QStringLiteral("Temp\u00e9rature \u00b0C");
    unicodeName.area = Area::HoldingRegister;
    unicodeName.address = 65535;
    unicodeName.type = ValueType::UInt16;

    Tag level;
    level.id = 9;
    level.name = QStringLiteral("Level");
    level.area = Area::InputRegister;
    level.address = 0;
    level.gain = 0.001;

    p.tags = {temp, pressure, unicodeName, level};
    p.nextTagId = 10;
    return p;
}

QByteArray json(const char *text)
{
    return QByteArray(text);
}

void writeBytes(const QString &path, const QByteArray &data)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
}

} // namespace

class TstProjectIO : public QObject
{
    Q_OBJECT
private slots:
    void roundTrip();
    void writesSchemaKeys();
    void minimalJsonUsesDefaults();
    void missingTagFieldsUseDefaults();
    void nextTagIdIsRaisedAboveMaxId();
    void storedNextTagIdIsKept();
    void boundaryValuesAccepted();
    void invalidInputRejected_data();
    void invalidInputRejected();
    void saveAndLoadFile();
    void saveOverwritesExistingFile();
    void loadMissingFileFailsWithPath();
    void loadCorruptFileFailsWithPath();
    void unicodeNamesSurvive();
    void roundTripAllTypesAndByteOrders_data();
    void roundTripAllTypesAndByteOrders();
    void byteOrderDefaultsToAbcd();
    void addressSpaceLimit_data();
    void addressSpaceLimit();
    void unitIdRange();
    void legacySingleRegisterWritesIsIgnored_data();
    void legacySingleRegisterWritesIsIgnored();
    void addressBaseRoundTrip_data();
    void addressBaseRoundTrip();
    void addressBaseDefaultsToZero();
    void addressBaseAlwaysWritten();
    void exampleProjectsLoad();
    void analyzedAndAnalysisRoundTrip();
    void analysisDefaultsWhenMissing();
    void analysisAlwaysWritten();
    void analysisBoundariesAccepted();
    void analysisRejected_data();
    void analysisRejected();
    void v126RoundTrip();
    void v126NamesWritten();
    void v126DefaultsWhenMissing();
    void v126RejectedValues_data();
    void v126RejectedValues();
    void v126MigratesAnalyzedTags();
    void v126MigratesWithoutAnalysisObject();
    void v126ExplicitKeysWinOverAnalyzed();
    void v126MethodFromModelPercents_data();
    void v126MethodFromModelPercents();
    void v126AnalyzedNotWritten();
    void v126AnalyzedStillValidated();
    void axisQuickAndAnalysisFlagsRoundTrip();
    void quickWrittenOnlyWhenSet();
    void quickNullMeansNoValue();
    void v12KeysDefaultWhenMissing();
    void writeLimitsRoundTrip();
    void writeLimitsWrittenOnlyWhenSet();
    void writeLimitsNullAndMissingMeanNoLimit();
    void writeLimitsRejected_data();
    void writeLimitsRejected();
    void v121KeysDefaultWhenMissing();
    void hiddenColumnsDefaultToCompact();
    void hiddenColumnsRoundTrip();
    void hiddenColumnsMissingIsCompact();
    void hiddenColumnsEmptyMeansAllVisible();
    void hiddenColumnsDuplicatesDropped();
    void hiddenColumnsAlwaysWritten();
    void hiddenColumnsRejected_data();
    void hiddenColumnsRejected();
    void encodingVerifiedRoundTrip();
    void encodingVerifiedWrittenOnlyWhenFalse();
    void encodingVerifiedMissingIsTrue();
    void encodingVerifiedRejected_data();
    void encodingVerifiedRejected();
    void confirmed16BitRoundTrip();
    void confirmed16BitWrittenOnlyWhenTrue();
    void confirmed16BitMissingIsFalse();
    void confirmed16BitRejected_data();
    void confirmed16BitRejected();
};

void TstProjectIO::roundTrip()
{
    const Project original = sampleProject();
    const QByteArray data = projectToJson(original);

    QString error;
    const std::optional<Project> loaded = projectFromJson(data, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, original);
}

void TstProjectIO::writesSchemaKeys()
{
    const QJsonObject root = QJsonDocument::fromJson(projectToJson(sampleProject())).object();
    QCOMPARE(root.value(QStringLiteral("format")).toString(), QStringLiteral("modbus-control-project"));
    QCOMPARE(root.value(QStringLiteral("version")).toInt(), kProjectFormatVersion);
    QCOMPARE(root.value(QStringLiteral("nextTagId")).toInt(), 10);

    const QJsonObject conn = root.value(QStringLiteral("connection")).toObject();
    QCOMPARE(conn.value(QStringLiteral("host")).toString(), QStringLiteral("10.0.0.5"));
    QCOMPARE(conn.value(QStringLiteral("port")).toInt(), 1502);
    QCOMPARE(conn.value(QStringLiteral("unitId")).toInt(), 7);
    QCOMPARE(conn.value(QStringLiteral("timeoutMs")).toInt(), 2500);
    QCOMPARE(conn.value(QStringLiteral("pollPeriodMs")).toInt(), 200);
    QVERIFY(!conn.contains(QStringLiteral("singleRegisterWrites")));

    const QJsonArray tags = root.value(QStringLiteral("tags")).toArray();
    QCOMPARE(tags.size(), qsizetype(4));
    const QJsonObject pressure = tags.at(1).toObject();
    QCOMPARE(pressure.value(QStringLiteral("area")).toString(), QStringLiteral("input"));
    QCOMPARE(pressure.value(QStringLiteral("type")).toString(), QStringLiteral("uint16"));
    QCOMPARE(pressure.value(QStringLiteral("plotted")).toBool(), true);
    QCOMPARE(tags.at(0).toObject().value(QStringLiteral("area")).toString(), QStringLiteral("holding"));

    // byteOrder is written for every tag, including 16-bit ones.
    for (qsizetype i = 0; i < tags.size(); ++i)
        QVERIFY(tags.at(i).toObject().contains(QStringLiteral("byteOrder")));
    QCOMPARE(tags.at(0).toObject().value(QStringLiteral("byteOrder")).toString(), QStringLiteral("ABCD"));
}

void TstProjectIO::minimalJsonUsesDefaults()
{
    QString error;
    const auto project = projectFromJson(
        json(R"({"format":"modbus-control-project","version":1})"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->connection, ConnectionSettings{});
    QVERIFY(project->tags.isEmpty());
    QCOMPARE(project->nextTagId, TagId(1));
}

void TstProjectIO::missingTagFieldsUseDefaults()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":3}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->tags.size(), qsizetype(1));

    Tag expected;
    expected.id = 3;
    QCOMPARE(project->tags.first(), expected);
    QCOMPARE(project->tags.first().color, QStringLiteral("#1f77b4"));
    QCOMPARE(project->nextTagId, TagId(4));
}

void TstProjectIO::nextTagIdIsRaisedAboveMaxId()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"nextTagId":2,
        "tags":[{"id":7},{"id":4}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->nextTagId, TagId(8));
}

void TstProjectIO::storedNextTagIdIsKept()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"nextTagId":20,
        "tags":[{"id":7}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->nextTagId, TagId(20));
}

void TstProjectIO::boundaryValuesAccepted()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "connection":{"port":1},
        "tags":[{"id":1,"address":0},{"id":2,"address":65535}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->connection.port, quint16(1));
    QCOMPARE(project->tags.at(0).address, quint16(0));
    QCOMPARE(project->tags.at(1).address, quint16(65535));

    const auto maxPort = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"connection":{"port":65535}
    })"));
    QVERIFY(maxPort.has_value());
    QCOMPARE(maxPort->connection.port, quint16(65535));

    const auto minMs = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "connection":{"timeoutMs":100,"pollPeriodMs":100}
    })"), &error);
    QVERIFY2(minMs.has_value(), qPrintable(error));
    QCOMPARE(minMs->connection.timeoutMs, 100);
    QCOMPARE(minMs->connection.pollPeriodMs, 100);

    const auto maxMs = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "connection":{"timeoutMs":60000,"pollPeriodMs":60000}
    })"), &error);
    QVERIFY2(maxMs.has_value(), qPrintable(error));
    QCOMPARE(maxMs->connection.timeoutMs, 60000);
    QCOMPARE(maxMs->connection.pollPeriodMs, 60000);
}

void TstProjectIO::invalidInputRejected_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("fragment");

    QTest::newRow("not JSON")
        << json("{not json") << QStringLiteral("Invalid JSON");
    QTest::newRow("top-level array")
        << json("[]") << QStringLiteral("top-level");
    QTest::newRow("format missing")
        << json(R"({"version":1})") << QStringLiteral("format");
    QTest::newRow("format different")
        << json(R"({"format":"something-else","version":1})") << QStringLiteral("format");
    QTest::newRow("version missing")
        << json(R"({"format":"modbus-control-project"})") << QStringLiteral("version");
    QTest::newRow("version fractional")
        << json(R"({"format":"modbus-control-project","version":1.5})") << QStringLiteral("integer");
    QTest::newRow("version string")
        << json(R"({"format":"modbus-control-project","version":"1"})") << QStringLiteral("integer");
    QTest::newRow("version too new")
        << json(R"({"format":"modbus-control-project","version":2})") << QStringLiteral("newer");
    QTest::newRow("version zero")
        << json(R"({"format":"modbus-control-project","version":0})") << QStringLiteral("version");
    QTest::newRow("unknown area")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"area":"coil"}]})")
        << QStringLiteral("area");
    QTest::newRow("unknown type")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"type":"float64"}]})")
        << QStringLiteral("type");
    QTest::newRow("unknown byteOrder")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"type":"int32","byteOrder":"XYZW"}]})")
        << QStringLiteral("byteOrder");
    QTest::newRow("byteOrder lower case")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"type":"float32","byteOrder":"cdab"}]})")
        << QStringLiteral("byteOrder");
    QTest::newRow("byteOrder not a string")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"type":"float32","byteOrder":1}]})")
        << QStringLiteral("byteOrder");
    QTest::newRow("unitId 248")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"unitId":248}})")
        << QStringLiteral("unitId");
    QTest::newRow("unitId 255")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"unitId":255}})")
        << QStringLiteral("unitId");
    QTest::newRow("unitId negative")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"unitId":-1}})")
        << QStringLiteral("unitId");
    QTest::newRow("duplicate tag id")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1},{"id":1}]})")
        << QStringLiteral("Duplicate");
    QTest::newRow("tag id zero")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":0}]})")
        << QStringLiteral("id");
    QTest::newRow("tag id missing")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"name":"x"}]})")
        << QStringLiteral("id");
    QTest::newRow("address too large")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"address":65536}]})")
        << QStringLiteral("address");
    QTest::newRow("address negative")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"address":-1}]})")
        << QStringLiteral("address");
    QTest::newRow("port zero")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"port":0}})")
        << QStringLiteral("port");
    QTest::newRow("port too large")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"port":65536}})")
        << QStringLiteral("port");
    QTest::newRow("port fractional")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"port":502.5}})")
        << QStringLiteral("port");
    QTest::newRow("timeout below 100")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"timeoutMs":99}})")
        << QStringLiteral("timeoutMs");
    QTest::newRow("timeout zero")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"timeoutMs":0}})")
        << QStringLiteral("timeoutMs");
    QTest::newRow("timeout above 60000")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"timeoutMs":60001}})")
        << QStringLiteral("timeoutMs");
    QTest::newRow("period below 100")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"pollPeriodMs":50}})")
        << QStringLiteral("pollPeriodMs");
    QTest::newRow("period above 60000")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"pollPeriodMs":60001}})")
        << QStringLiteral("pollPeriodMs");
    QTest::newRow("singleRegisterWrites number")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"singleRegisterWrites":1}})")
        << QStringLiteral("singleRegisterWrites");
    QTest::newRow("singleRegisterWrites string")
        << json(R"({"format":"modbus-control-project","version":1,"connection":{"singleRegisterWrites":"true"}})")
        << QStringLiteral("singleRegisterWrites");
    QTest::newRow("addressBase 2")
        << json(R"({"format":"modbus-control-project","version":1,"addressBase":2})")
        << QStringLiteral("addressBase");
    QTest::newRow("addressBase negative")
        << json(R"({"format":"modbus-control-project","version":1,"addressBase":-1})")
        << QStringLiteral("addressBase");
    QTest::newRow("addressBase string")
        << json(R"({"format":"modbus-control-project","version":1,"addressBase":"1"})")
        << QStringLiteral("addressBase");
    QTest::newRow("axis up")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"axis":"up"}]})")
        << QStringLiteral("axis");
    QTest::newRow("axis upper case")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"axis":"Right"}]})")
        << QStringLiteral("axis");
    QTest::newRow("quickA string")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"quickA":"x"}]})")
        << QStringLiteral("quickA");
    QTest::newRow("quickB boolean")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"quickB":true}]})")
        << QStringLiteral("quickB");
}

void TstProjectIO::invalidInputRejected()
{
    QFETCH(QByteArray, text);
    QFETCH(QString, fragment);

    QString error;
    const auto project = projectFromJson(text, &error);
    QVERIFY(!project.has_value());
    QVERIFY2(error.contains(fragment, Qt::CaseInsensitive),
             qPrintable(QStringLiteral("error was: %1").arg(error)));
}

void TstProjectIO::saveAndLoadFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plant.mbproj"));

    const Project original = sampleProject();
    QString error;
    QVERIFY2(saveProject(path, original, &error), qPrintable(error));
    QVERIFY(QFile::exists(path));

    const auto loaded = loadProject(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, original);
}

void TstProjectIO::saveOverwritesExistingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plant.mbproj"));

    QString error;
    QVERIFY(saveProject(path, sampleProject(), &error));

    Project second;
    second.nextTagId = 1;
    QVERIFY2(saveProject(path, second, &error), qPrintable(error));

    const auto loaded = loadProject(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, second);
}

void TstProjectIO::loadMissingFileFailsWithPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("does-not-exist.mbproj"));

    QString error;
    const auto loaded = loadProject(path, &error);
    QVERIFY(!loaded.has_value());
    QVERIFY2(error.contains(path), qPrintable(QStringLiteral("error was: %1").arg(error)));
}

void TstProjectIO::loadCorruptFileFailsWithPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("corrupt.mbproj"));
    writeBytes(path, json("{\"format\":"));

    QString error;
    const auto loaded = loadProject(path, &error);
    QVERIFY(!loaded.has_value());
    QVERIFY2(error.contains(path), qPrintable(QStringLiteral("error was: %1").arg(error)));
}

void TstProjectIO::unicodeNamesSurvive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("unicode.mbproj"));

    Project p;
    Tag tag;
    tag.id = 1;
    tag.name = QStringLiteral("Temp\u00e9rature \u00b0C");
    tag.unit = QStringLiteral("\u00b0C");
    p.tags.append(tag);
    p.nextTagId = 2;

    QString error;
    QVERIFY2(saveProject(path, p, &error), qPrintable(error));
    const auto loaded = loadProject(path, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->tags.first().name, QStringLiteral("Temp\u00e9rature \u00b0C"));
    QCOMPARE(*loaded, p);
}

void TstProjectIO::roundTripAllTypesAndByteOrders_data()
{
    QTest::addColumn<ValueType>("type");
    QTest::addColumn<ByteOrder>("order");

    const ValueType types[] = {ValueType::Int16, ValueType::UInt16, ValueType::Int32, ValueType::UInt32,
                               ValueType::Float32};
    const ByteOrder orders[] = {ByteOrder::ABCD, ByteOrder::CDAB, ByteOrder::BADC, ByteOrder::DCBA};
    for (const ValueType type : types) {
        for (const ByteOrder order : orders)
            QTest::addRow("%s %s", qPrintable(toString(type)), qPrintable(toString(order))) << type << order;
    }
}

void TstProjectIO::roundTripAllTypesAndByteOrders()
{
    QFETCH(ValueType, type);
    QFETCH(ByteOrder, order);

    Project p;
    Tag tag;
    tag.id = 4;
    tag.name = QStringLiteral("Tag");
    tag.area = Area::InputRegister;
    tag.address = 1000;
    tag.type = type;
    tag.byteOrder = order;
    tag.gain = 0.25;
    tag.offset = -3.5;
    tag.plotted = false;
    p.tags.append(tag);
    p.nextTagId = 5;

    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, p);
    QCOMPARE(static_cast<int>(loaded->tags.first().type), static_cast<int>(type));
    QCOMPARE(static_cast<int>(loaded->tags.first().byteOrder), static_cast<int>(order));
}

void TstProjectIO::byteOrderDefaultsToAbcd()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"type":"float32"}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(static_cast<int>(project->tags.first().type), static_cast<int>(ValueType::Float32));
    QCOMPARE(static_cast<int>(project->tags.first().byteOrder), static_cast<int>(ByteOrder::ABCD));
}

void TstProjectIO::addressSpaceLimit_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<int>("address");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("int16 at 65535") << QStringLiteral("int16") << 65535 << true;
    QTest::newRow("uint16 at 65535") << QStringLiteral("uint16") << 65535 << true;
    QTest::newRow("int32 at 65534") << QStringLiteral("int32") << 65534 << true;
    QTest::newRow("uint32 at 65534") << QStringLiteral("uint32") << 65534 << true;
    QTest::newRow("float32 at 65534") << QStringLiteral("float32") << 65534 << true;
    QTest::newRow("int32 at 65535") << QStringLiteral("int32") << 65535 << false;
    QTest::newRow("uint32 at 65535") << QStringLiteral("uint32") << 65535 << false;
    QTest::newRow("float32 at 65535") << QStringLiteral("float32") << 65535 << false;
}

void TstProjectIO::addressSpaceLimit()
{
    QFETCH(QString, type);
    QFETCH(int, address);
    QFETCH(bool, accepted);

    const QString text = QStringLiteral(
                             "{\"format\":\"modbus-control-project\",\"version\":1,"
                             "\"tags\":[{\"id\":1,\"type\":\"%1\",\"address\":%2}]}")
                             .arg(type)
                             .arg(address);
    QString error;
    const std::optional<Project> project = projectFromJson(text.toUtf8(), &error);
    if (accepted) {
        QVERIFY2(project.has_value(), qPrintable(error));
        QCOMPARE(project->tags.first().address, quint16(address));
    } else {
        QVERIFY(!project.has_value());
        QVERIFY2(error.contains(QStringLiteral("address")),
                 qPrintable(QStringLiteral("error was: %1").arg(error)));
    }
}

void TstProjectIO::unitIdRange()
{
    QString error;
    const auto low = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"connection":{"unitId":0}
    })"), &error);
    QVERIFY2(low.has_value(), qPrintable(error));
    QCOMPARE(low->connection.unitId, 0);

    const auto high = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"connection":{"unitId":247}
    })"), &error);
    QVERIFY2(high.has_value(), qPrintable(error));
    QCOMPARE(high->connection.unitId, 247);
}

void TstProjectIO::legacySingleRegisterWritesIsIgnored_data()
{
    QTest::addColumn<bool>("value");

    QTest::newRow("true") << true;
    QTest::newRow("false") << false;
}

// Files written before the FC06-only option was removed still load; the key is not written back.
void TstProjectIO::legacySingleRegisterWritesIsIgnored()
{
    QFETCH(bool, value);

    QString error;
    const auto project = projectFromJson(
        json(QStringLiteral(R"({"format":"modbus-control-project","version":1,
                "connection":{"port":502,"singleRegisterWrites":%1}})")
                 .arg(value ? QStringLiteral("true") : QStringLiteral("false"))
                 .toUtf8().constData()),
        &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->connection.port, quint16(502));

    const QByteArray out = projectToJson(*project);
    QVERIFY(!out.contains("singleRegisterWrites"));
    QVERIFY(projectFromJson(out, &error).has_value());
}

void TstProjectIO::addressBaseRoundTrip_data()
{
    QTest::addColumn<int>("base");
    QTest::newRow("0") << 0;
    QTest::newRow("1") << 1;
}

void TstProjectIO::addressBaseRoundTrip()
{
    QFETCH(int, base);
    Project p;
    p.addressBase = base;
    p.nextTagId = 1;

    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->addressBase, base);
    QCOMPARE(*loaded, p);
}

void TstProjectIO::addressBaseDefaultsToZero()
{
    QString error;
    const auto project = projectFromJson(json(R"({"format":"modbus-control-project","version":1})"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->addressBase, 0);
}

void TstProjectIO::addressBaseAlwaysWritten()
{
    const QJsonObject root = QJsonDocument::fromJson(projectToJson(Project{})).object();
    QVERIFY(root.contains(QStringLiteral("addressBase")));
    QCOMPARE(root.value(QStringLiteral("addressBase")).toInt(-1), 0);
}

void TstProjectIO::exampleProjectsLoad()
{
    // Find the repository root by walking up from the test executable.
    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists(QStringLiteral("examples/demo.mbproj"))) {
        QVERIFY2(dir.cdUp(), "could not locate examples/demo.mbproj above the test executable");
    }

    QString error;
    const auto demo = loadProject(dir.filePath(QStringLiteral("examples/demo.mbproj")), &error);
    QVERIFY2(demo.has_value(), qPrintable(error));
    QCOMPARE(demo->addressBase, 0);
    QCOMPARE(demo->tags.size(), 11);

    const auto cm50 = loadProject(dir.filePath(QStringLiteral("examples/cm50.mbproj")), &error);
    QVERIFY2(cm50.has_value(), qPrintable(error));
    QCOMPARE(cm50->addressBase, 1);
    QCOMPARE(cm50->connection.host, QStringLiteral("192.168.1.50"));
    QCOMPARE(cm50->connection.port, quint16(502));
    QCOMPARE(cm50->tags.size(), 6);
    // Register 5009 (as printed in the manual) is protocol address 5008.
    QCOMPARE(cm50->tags.at(0).name, QStringLiteral("Loop 1 Process Variable"));
    QCOMPARE(int(cm50->tags.at(0).address) + cm50->addressBase, 5009);
    QCOMPARE(int(cm50->tags.at(0).address), 5008);
}

void TstProjectIO::analyzedAndAnalysisRoundTrip()
{
    Project original = sampleProject();
    original.analysis.percents = {0.0, 10.0, 63.2, 100.0};
    original.analysis.reference = ReferenceMode::WindowMean;
    original.analysis.referenceWindowS = 2.5;
    original.analysis.smoothingS = 3.0;
    original.analysis.modelPercentA = 10.0;
    original.analysis.modelPercentB = 90.0;
    original.analysis.method = ModelMethod::Custom;
    original.analysis.pvTagId = 2;
    original.analysis.setpointTagIds = {1, 9};
    original.analysis.deltaCoTagId = 2;
    original.analysis.manualDeltaCo = -1.5;
    original.analysis.detailedLabels = true;

    const QByteArray data = projectToJson(original);
    QString error;
    const std::optional<Project> loaded = projectFromJson(data, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, original);
    QVERIFY(loaded->analysis.pvTagId == 2);

    // The per-tag "analyzed" flag is no longer written, so it reads back false.
    Project flagged = sampleProject();
    flagged.tags[0].analyzed = true;
    const auto flaggedAgain = projectFromJson(projectToJson(flagged), &error);
    QVERIFY2(flaggedAgain.has_value(), qPrintable(error));
    QVERIFY(!flaggedAgain->tags[0].analyzed);

    // The cursor reference and the typed-ΔCO default also survive a round trip.
    Project cursorMode = sampleProject();
    cursorMode.analysis.deltaCoTagId = 0;
    const auto again = projectFromJson(projectToJson(cursorMode), &error);
    QVERIFY2(again.has_value(), qPrintable(error));
    QCOMPARE(again->analysis.reference, ReferenceMode::CursorValue);
    QCOMPARE(*again, cursorMode);
}

void TstProjectIO::analysisDefaultsWhenMissing()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"analyzed":true},{"id":2}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    // The analyzed tag migrates to the PV and the setpoint list (see v126MigratesAnalyzedTags).
    AnalysisSettings expected;
    expected.pvTagId = 1;
    expected.setpointTagIds = {1};
    QCOMPARE(project->analysis, expected);
    QVERIFY(project->tags.at(0).analyzed);
    QVERIFY(!project->tags.at(1).analyzed);
}

void TstProjectIO::analysisAlwaysWritten()
{
    const QJsonObject root = QJsonDocument::fromJson(projectToJson(Project{})).object();
    QVERIFY(root.value(QStringLiteral("analysis")).isObject());
    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    for (const char *key : {"percents", "reference", "referenceWindowS", "smoothingS",
                            "modelPercents", "deltaCoTag", "deltaCo", "method", "pvTag",
                            "setpointTags", "pvRange", "coRange", "tuningRule", "lambdaS",
                            "timeUnit"})
        QVERIFY2(analysis.contains(QString::fromLatin1(key)), key);
    QCOMPARE(analysis.value(QStringLiteral("reference")).toString(), QStringLiteral("cursor"));
    QCOMPARE(analysis.value(QStringLiteral("percents")).toArray().size(), qsizetype(2));
    QCOMPARE(analysis.value(QStringLiteral("modelPercents")).toArray().size(), qsizetype(2));

    Project withTag;
    Tag t;
    t.id = 1;
    withTag.tags.append(t);
    const QJsonArray tags = QJsonDocument::fromJson(projectToJson(withTag)).object()
                                .value(QStringLiteral("tags")).toArray();
    const QJsonObject tag = tags.at(0).toObject();
    QVERIFY(!tag.contains(QStringLiteral("analyzed")));
}

void TstProjectIO::analysisBoundariesAccepted()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "analysis":{
            "percents":[0,100,0,100,0,100,0,100,0,100,0,100,0,100,0,100,0,100,0,100],
            "reference":"mean","referenceWindowS":3600,"smoothingS":0,
            "modelPercents":[0,99.9],"deltaCoTag":2147483647,"deltaCo":-0.25
        }
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->analysis.percents.size(), qsizetype(20));
    QCOMPARE(project->analysis.reference, ReferenceMode::WindowMean);
    QCOMPARE(project->analysis.referenceWindowS, 3600.0);
    QCOMPARE(project->analysis.smoothingS, 0.0);
    QCOMPARE(project->analysis.modelPercentA, 0.0);
    QCOMPARE(project->analysis.modelPercentB, 99.9);
    QCOMPARE(project->analysis.deltaCoTagId, TagId(2147483647));
    QCOMPARE(project->analysis.manualDeltaCo, -0.25);

    const auto empty = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"analysis":{"percents":[],"smoothingS":3600}
    })"), &error);
    QVERIFY2(empty.has_value(), qPrintable(error));
    QVERIFY(empty->analysis.percents.isEmpty());
    QCOMPARE(empty->analysis.smoothingS, 3600.0);
}

void TstProjectIO::analysisRejected_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("fragment");

    const QByteArray head = R"({"format":"modbus-control-project","version":1,"analysis":)";
    QTest::newRow("percents 101")
        << head + R"({"percents":[101]}})" << QStringLiteral("analysis.percents");
    QTest::newRow("21 percents")
        << head + R"({"percents":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}})"
        << QStringLiteral("analysis.percents");
    QTest::newRow("percents not an array")
        << head + R"({"percents":5}})" << QStringLiteral("analysis.percents");
    QTest::newRow("reference foo")
        << head + R"({"reference":"foo"}})" << QStringLiteral("analysis.reference");
    QTest::newRow("referenceWindowS 0")
        << head + R"({"referenceWindowS":0}})" << QStringLiteral("analysis.referenceWindowS");
    QTest::newRow("referenceWindowS above 3600")
        << head + R"({"referenceWindowS":3600.5}})" << QStringLiteral("analysis.referenceWindowS");
    QTest::newRow("smoothingS -1")
        << head + R"({"smoothingS":-1}})" << QStringLiteral("analysis.smoothingS");
    QTest::newRow("modelPercents a > b")
        << head + R"({"modelPercents":[63.2,0]}})" << QStringLiteral("analysis.modelPercents");
    QTest::newRow("modelPercents three entries")
        << head + R"({"modelPercents":[0,50,60]}})" << QStringLiteral("analysis.modelPercents");
    QTest::newRow("modelPercents b = 100")
        << head + R"({"modelPercents":[0,100]}})" << QStringLiteral("analysis.modelPercents");
    QTest::newRow("deltaCoTag -1")
        << head + R"({"deltaCoTag":-1}})" << QStringLiteral("analysis.deltaCoTag");
    QTest::newRow("deltaCo as string")
        << head + R"({"deltaCo":"1.5"}})" << QStringLiteral("analysis.deltaCo");
    QTest::newRow("analysis not an object")
        << head + R"([]})" << QStringLiteral("\"analysis\" must be an object");
    QTest::newRow("showModel yes")
        << head + R"({"showModel":"yes"}})" << QStringLiteral("analysis.showModel");
    QTest::newRow("guideLines 1")
        << head + R"({"guideLines":1}})" << QStringLiteral("analysis.guideLines");
}

void TstProjectIO::analysisRejected()
{
    QFETCH(QByteArray, text);
    QFETCH(QString, fragment);
    QString error;
    const auto project = projectFromJson(text, &error);
    QVERIFY(!project.has_value());
    QVERIFY2(error.contains(fragment), qPrintable(error));
}

void TstProjectIO::v126RoundTrip()
{
    const ModelMethod methods[] = {ModelMethod::ZeroTo632, ModelMethod::Smith,
                                   ModelMethod::Sundaresan, ModelMethod::Custom};
    const TuningRule rules[] = {TuningRule::ZieglerNichols, TuningRule::CohenCoon,
                                TuningRule::ImcLambda, TuningRule::Simc};
    for (ModelMethod method : methods) {
        for (TuningRule rule : rules) {
            for (bool minutes : {false, true}) {
                Project p = sampleProject();
                p.analysis.method = method;
                p.analysis.tuningRule = rule;
                p.analysis.timeInMinutes = minutes;
                p.analysis.pvTagId = 2;
                p.analysis.setpointTagIds = {1, 9, 42};
                p.analysis.pvRangeMin = -5.5;
                p.analysis.pvRangeMax = 120.25;
                p.analysis.coRangeMin = -10.0;
                p.analysis.coRangeMax = 90.0;
                p.analysis.lambdaS = 45.5;

                QString error;
                const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
                QVERIFY2(loaded.has_value(), qPrintable(error));
                QCOMPARE(*loaded, p);
                QVERIFY(loaded->analysis.method == method);
                QVERIFY(loaded->analysis.tuningRule == rule);
                QVERIFY(loaded->analysis.timeInMinutes == minutes);
                QCOMPARE(loaded->analysis.pvRangeMax.value(), 120.25);
            }
        }
    }

    // pvRange null (no range) and an empty setpoint list survive as well.
    Project none = sampleProject();
    none.analysis.setpointTagIds.clear();
    const auto again = projectFromJson(projectToJson(none));
    QVERIFY(again.has_value());
    QVERIFY(!again->analysis.pvRangeMin.has_value());
    QVERIFY(!again->analysis.pvRangeMax.has_value());
    QVERIFY(again->analysis.setpointTagIds.isEmpty());
    QCOMPARE(*again, none);

    // Zero-width ranges (the UI can produce them) must stay loadable. PV: written as null and
    // read back as not set. CO: written and read back as it is.
    Project flat = sampleProject();
    flat.analysis.pvRangeMin = 5.0;
    flat.analysis.pvRangeMax = 5.0;
    flat.analysis.coRangeMin = 30.0;
    flat.analysis.coRangeMax = 30.0;
    const QJsonObject flatAnalysis = QJsonDocument::fromJson(projectToJson(flat)).object()
                                         .value(QStringLiteral("analysis")).toObject();
    QVERIFY(flatAnalysis.value(QStringLiteral("pvRange")).isNull());
    QString flatError;
    const auto flatLoaded = projectFromJson(projectToJson(flat), &flatError);
    QVERIFY2(flatLoaded.has_value(), qPrintable(flatError));
    QVERIFY(!flatLoaded->analysis.pvRangeMin.has_value());
    QVERIFY(!flatLoaded->analysis.pvRangeMax.has_value());
    QCOMPARE(flatLoaded->analysis.coRangeMin, 30.0);
    QCOMPARE(flatLoaded->analysis.coRangeMax, 30.0);
}

void TstProjectIO::v126NamesWritten()
{
    Project p;
    p.analysis.method = ModelMethod::Sundaresan;
    p.analysis.tuningRule = TuningRule::CohenCoon;
    p.analysis.timeInMinutes = true;
    p.analysis.setpointTagIds = {3, 4};
    p.analysis.pvTagId = 4;

    const QJsonObject analysis = QJsonDocument::fromJson(projectToJson(p)).object()
                                     .value(QStringLiteral("analysis")).toObject();
    QCOMPARE(analysis.value(QStringLiteral("method")).toString(), QStringLiteral("sundaresan"));
    QCOMPARE(analysis.value(QStringLiteral("tuningRule")).toString(), QStringLiteral("cohen-coon"));
    QCOMPARE(analysis.value(QStringLiteral("timeUnit")).toString(), QStringLiteral("min"));
    QCOMPARE(analysis.value(QStringLiteral("pvTag")).toInt(), 4);
    QCOMPARE(analysis.value(QStringLiteral("setpointTags")).toArray().size(), qsizetype(2));
    QVERIFY(analysis.value(QStringLiteral("pvRange")).isNull());
    const QJsonArray coRange = analysis.value(QStringLiteral("coRange")).toArray();
    QCOMPARE(coRange.size(), qsizetype(2));
    QCOMPARE(coRange.at(0).toDouble(), 0.0);
    QCOMPARE(coRange.at(1).toDouble(), 100.0);

    p.analysis.method = ModelMethod::Smith;
    p.analysis.tuningRule = TuningRule::Simc;
    p.analysis.timeInMinutes = false;
    p.analysis.pvRangeMin = 0.0;
    p.analysis.pvRangeMax = 50.0;
    const QJsonObject second = QJsonDocument::fromJson(projectToJson(p)).object()
                                   .value(QStringLiteral("analysis")).toObject();
    QCOMPARE(second.value(QStringLiteral("method")).toString(), QStringLiteral("smith"));
    QCOMPARE(second.value(QStringLiteral("tuningRule")).toString(), QStringLiteral("simc"));
    QCOMPARE(second.value(QStringLiteral("timeUnit")).toString(), QStringLiteral("s"));
    const QJsonArray pvRange = second.value(QStringLiteral("pvRange")).toArray();
    QCOMPARE(pvRange.at(1).toDouble(), 50.0);
}

void TstProjectIO::v126DefaultsWhenMissing()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"analysis":{}
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    const AnalysisSettings &a = project->analysis;
    QVERIFY(a.method == ModelMethod::ZeroTo632);
    QVERIFY(a.tuningRule == TuningRule::ZieglerNichols);
    QVERIFY(!a.timeInMinutes);
    QCOMPARE(a.lambdaS, 0.0);
    QVERIFY(!a.pvRangeMin.has_value());
    QVERIFY(!a.pvRangeMax.has_value());
    QCOMPARE(a.coRangeMin, 0.0);
    QCOMPARE(a.coRangeMax, 100.0);
    QCOMPARE(a.pvTagId, TagId(0));
    QVERIFY(a.setpointTagIds.isEmpty());
    QVERIFY(a.showModel);

    // "pvRange": null is the explicit form of "no range".
    const auto nullRange = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"analysis":{"pvRange":null}
    })"), &error);
    QVERIFY2(nullRange.has_value(), qPrintable(error));
    QVERIFY(!nullRange->analysis.pvRangeMin.has_value());

    // A file with equal ends loads: pvRange = not set, coRange keeps its values.
    const auto flat = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "analysis":{"pvRange":[5,5],"coRange":[7,7]}
    })"), &error);
    QVERIFY2(flat.has_value(), qPrintable(error));
    QVERIFY(!flat->analysis.pvRangeMin.has_value());
    QVERIFY(!flat->analysis.pvRangeMax.has_value());
    QCOMPARE(flat->analysis.coRangeMin, 7.0);
    QCOMPARE(flat->analysis.coRangeMax, 7.0);
}

void TstProjectIO::v126RejectedValues_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("fragment");

    const QByteArray head = R"({"format":"modbus-control-project","version":1,"analysis":)";
    QTest::newRow("method unknown")
        << head + R"({"method":"foo"}})" << QStringLiteral("analysis.method");
    QTest::newRow("method not a string")
        << head + R"({"method":5}})" << QStringLiteral("analysis.method");
    QTest::newRow("pvTag -1")
        << head + R"({"pvTag":-1}})" << QStringLiteral("analysis.pvTag");
    QTest::newRow("pvTag 1.5")
        << head + R"({"pvTag":1.5}})" << QStringLiteral("analysis.pvTag");
    QTest::newRow("pvTag as string")
        << head + R"({"pvTag":"3"}})" << QStringLiteral("analysis.pvTag");
    QTest::newRow("setpointTags not an array")
        << head + R"({"setpointTags":3}})" << QStringLiteral("analysis.setpointTags");
    QTest::newRow("setpointTags with 0")
        << head + R"({"setpointTags":[1,0]}})" << QStringLiteral("analysis.setpointTags");
    QTest::newRow("setpointTags with 1.5")
        << head + R"({"setpointTags":[1.5]}})" << QStringLiteral("analysis.setpointTags");
    QTest::newRow("setpointTags with string")
        << head + R"({"setpointTags":["2"]}})" << QStringLiteral("analysis.setpointTags");
    QTest::newRow("pvRange one number")
        << head + R"({"pvRange":[1]}})" << QStringLiteral("analysis.pvRange");
    QTest::newRow("pvRange not an array")
        << head + R"({"pvRange":3}})" << QStringLiteral("analysis.pvRange");
    QTest::newRow("pvRange with a string")
        << head + R"({"pvRange":[0,"100"]}})" << QStringLiteral("analysis.pvRange");
    QTest::newRow("coRange null")
        << head + R"({"coRange":null}})" << QStringLiteral("analysis.coRange");
    QTest::newRow("coRange three numbers")
        << head + R"({"coRange":[0,50,100]}})" << QStringLiteral("analysis.coRange");
    QTest::newRow("tuningRule unknown")
        << head + R"({"tuningRule":"pid"}})" << QStringLiteral("analysis.tuningRule");
    QTest::newRow("lambdaS negative")
        << head + R"({"lambdaS":-1}})" << QStringLiteral("analysis.lambdaS");
    QTest::newRow("lambdaS as string")
        << head + R"({"lambdaS":"10"}})" << QStringLiteral("analysis.lambdaS");
    QTest::newRow("timeUnit hours")
        << head + R"({"timeUnit":"h"}})" << QStringLiteral("analysis.timeUnit");
    QTest::newRow("timeUnit not a string")
        << head + R"({"timeUnit":1}})" << QStringLiteral("analysis.timeUnit");
}

void TstProjectIO::v126RejectedValues()
{
    QFETCH(QByteArray, text);
    QFETCH(QString, fragment);
    QString error;
    const auto project = projectFromJson(text, &error);
    QVERIFY(!project.has_value());
    QVERIFY2(error.contains(fragment), qPrintable(error));
}

void TstProjectIO::v126MigratesAnalyzedTags()
{
    // A file from before v1.2.6: no method, pvTag or setpointTags; the analysed tags are 2 and 4.
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1},{"id":2,"analyzed":true},{"id":4,"analyzed":true},{"id":5,"analyzed":false}],
        "analysis":{"percents":[0,63.2],"modelPercents":[0,63.2]}
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    const AnalysisSettings &a = project->analysis;
    QCOMPARE(a.pvTagId, TagId(2));
    QVERIFY(a.setpointTagIds == (QList<TagId>{2, 4}));
    QVERIFY(a.method == ModelMethod::ZeroTo632);
    QVERIFY(a.showModel);
    QVERIFY(project->tags.at(1).analyzed);
    QVERIFY(!project->tags.at(3).analyzed);

    // The CO tag is analysed and listed before the PV: the PV is the first analysed non-CO tag.
    const auto coFirst = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"analyzed":true},{"id":2,"analyzed":true}],
        "analysis":{"deltaCoTag":1}
    })"), &error);
    QVERIFY2(coFirst.has_value(), qPrintable(error));
    QCOMPARE(coFirst->analysis.pvTagId, TagId(2));
    QVERIFY(coFirst->analysis.setpointTagIds == (QList<TagId>{1, 2}));

    // Only the CO is analysed: fall back to it.
    const auto onlyCo = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"analyzed":true}],
        "analysis":{"deltaCoTag":1}
    })"), &error);
    QVERIFY2(onlyCo.has_value(), qPrintable(error));
    QCOMPARE(onlyCo->analysis.pvTagId, TagId(1));

    // No analysed tag at all: no PV and no setpoint tags.
    const auto none = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"tags":[{"id":1}]
    })"), &error);
    QVERIFY2(none.has_value(), qPrintable(error));
    QCOMPARE(none->analysis.pvTagId, TagId(0));
    QVERIFY(none->analysis.setpointTagIds.isEmpty());
}

void TstProjectIO::v126MigratesWithoutAnalysisObject()
{
    // The analysed flags are still the only record of the analysis when there is no "analysis".
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":3,"analyzed":true}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->analysis.pvTagId, TagId(3));
    QVERIFY(project->analysis.setpointTagIds == (QList<TagId>{3}));
    QVERIFY(project->analysis.method == ModelMethod::ZeroTo632);
}

void TstProjectIO::v126ExplicitKeysWinOverAnalyzed()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":2,"analyzed":true},{"id":4,"analyzed":true}],
        "analysis":{"pvTag":4,"setpointTags":[2]}
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->analysis.pvTagId, TagId(4));
    QVERIFY(project->analysis.setpointTagIds == (QList<TagId>{2}));

    // An explicit pvTag of 0 means "none" even when tags are analysed.
    const auto zero = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":2,"analyzed":true}],
        "analysis":{"pvTag":0,"setpointTags":[]}
    })"), &error);
    QVERIFY2(zero.has_value(), qPrintable(error));
    QCOMPARE(zero->analysis.pvTagId, TagId(0));
    QVERIFY(zero->analysis.setpointTagIds.isEmpty());
}

void TstProjectIO::v126MethodFromModelPercents_data()
{
    QTest::addColumn<QByteArray>("analysis");
    QTest::addColumn<bool>("zeroTo632");
    QTest::addColumn<double>("a");
    QTest::addColumn<double>("b");

    QTest::newRow("0 / 63.2 is the two-point method")
        << QByteArray(R"({"modelPercents":[0,63.2]})") << true << 0.0 << 63.2;
    QTest::newRow("missing modelPercents is 0 / 63.2")
        << QByteArray(R"({"percents":[0,63.2]})") << true << 0.0 << 63.2;
    QTest::newRow("10 / 70 is Custom")
        << QByteArray(R"({"modelPercents":[10,70]})") << false << 10.0 << 70.0;
    QTest::newRow("0 / 63 is Custom")
        << QByteArray(R"({"modelPercents":[0,63]})") << false << 0.0 << 63.0;
}

void TstProjectIO::v126MethodFromModelPercents()
{
    QFETCH(QByteArray, analysis);
    QFETCH(bool, zeroTo632);
    QFETCH(double, a);
    QFETCH(double, b);
    QString error;
    const auto project = projectFromJson(
        QByteArray(R"({"format":"modbus-control-project","version":1,"analysis":)") + analysis + "}",
        &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY((project->analysis.method == ModelMethod::ZeroTo632) == zeroTo632);
    QCOMPARE(project->analysis.modelPercentA, a);
    QCOMPARE(project->analysis.modelPercentB, b);

    // An explicit "method" always wins over the modelPercents rule.
    const auto explicitSmith = projectFromJson(
        QByteArray(R"({"format":"modbus-control-project","version":1,"analysis":{"method":"smith","modelPercents":[10,70]}})"),
        &error);
    QVERIFY2(explicitSmith.has_value(), qPrintable(error));
    QVERIFY(explicitSmith->analysis.method == ModelMethod::Smith);
}

void TstProjectIO::v126AnalyzedNotWritten()
{
    Project p = sampleProject();
    p.tags[0].analyzed = true;
    const QJsonArray tags = QJsonDocument::fromJson(projectToJson(p)).object()
                                .value(QStringLiteral("tags")).toArray();
    for (qsizetype i = 0; i < tags.size(); ++i)
        QVERIFY(!tags.at(i).toObject().contains(QStringLiteral("analyzed")));
}

void TstProjectIO::v126AnalyzedStillValidated()
{
    QString error;
    const auto bad = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"analyzed":"yes"}]
    })"), &error);
    QVERIFY(!bad.has_value());
    QVERIFY2(error.contains(QStringLiteral("analyzed")), qPrintable(error));
}

void TstProjectIO::axisQuickAndAnalysisFlagsRoundTrip()
{
    Project p = sampleProject();
    p.tags[0].axis = YAxis::Right;
    p.tags[0].quickA = 12.5;
    p.tags[0].quickB = -0.25;
    p.tags[1].axis = YAxis::Right; // input register, no quick values
    p.analysis.showModel = true;
    p.analysis.guideLines = false;

    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, p);
    QVERIFY(loaded->tags[0].axis == YAxis::Right);
    QVERIFY(loaded->tags[1].axis == YAxis::Right);
    QCOMPARE(*loaded->tags[0].quickA, 12.5);
    QCOMPARE(*loaded->tags[0].quickB, -0.25);
    QVERIFY(!loaded->tags[1].quickA.has_value());
    QVERIFY(loaded->analysis.showModel);
    QVERIFY(!loaded->analysis.guideLines);

    const QJsonObject root = QJsonDocument::fromJson(projectToJson(p)).object();
    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    QCOMPARE(analysis.value(QStringLiteral("showModel")).toBool(), true);
    QCOMPARE(analysis.value(QStringLiteral("guideLines")).toBool(), false);
}

void TstProjectIO::quickWrittenOnlyWhenSet()
{
    Project p;
    Tag onlyB;
    onlyB.id = 1;
    onlyB.quickB = 3.0;
    Tag none;
    none.id = 2;
    p.tags = {onlyB, none};
    p.nextTagId = 3;

    const QJsonArray tags = QJsonDocument::fromJson(projectToJson(p)).object()
                                .value(QStringLiteral("tags")).toArray();
    const QJsonObject first = tags.at(0).toObject();
    QVERIFY(!first.contains(QStringLiteral("quickA")));
    QCOMPARE(first.value(QStringLiteral("quickB")).toDouble(), 3.0);
    QCOMPARE(first.value(QStringLiteral("axis")).toString(), QStringLiteral("left"));

    const QJsonObject second = tags.at(1).toObject();
    QVERIFY(!second.contains(QStringLiteral("quickA")));
    QVERIFY(!second.contains(QStringLiteral("quickB")));
    QCOMPARE(second.value(QStringLiteral("axis")).toString(), QStringLiteral("left"));
}

void TstProjectIO::quickNullMeansNoValue()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"quickA":null,"quickB":4}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY(!project->tags.at(0).quickA.has_value());
    QCOMPARE(*project->tags.at(0).quickB, 4.0);
}

void TstProjectIO::v12KeysDefaultWhenMissing()
{
    // A v1.1-style file: none of the v1.2 keys are present.
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"name":"old","analyzed":true}],
        "analysis":{"percents":[0,63.2]}
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    const Tag &tag = project->tags.at(0);
    QVERIFY(tag.axis == YAxis::Left);
    QVERIFY(!tag.quickA.has_value());
    QVERIFY(!tag.quickB.has_value());
    QVERIFY(project->analysis.showModel); // default on since v1.2.6
    QVERIFY(project->analysis.guideLines);
    AnalysisSettings expected; // the analyzed tag becomes the PV (v1.2.6 migration)
    expected.pvTagId = 1;
    expected.setpointTagIds = {1};
    QCOMPARE(project->analysis, expected);
}

void TstProjectIO::writeLimitsRoundTrip()
{
    Project p = sampleProject();
    p.tags[0].writeMin = -5.5;
    p.tags[0].writeMax = 80.0;
    p.tags[0].quickA = 42.0; // inside the limits
    p.tags[1].writeMax = 0.0; // a limit on a non-writable tag is kept as stored

    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, p);
    QCOMPARE(*loaded->tags[0].writeMin, -5.5);
    QCOMPARE(*loaded->tags[0].writeMax, 80.0);
    QVERIFY(!loaded->tags[1].writeMin.has_value());
    QCOMPARE(*loaded->tags[1].writeMax, 0.0);
}

void TstProjectIO::writeLimitsWrittenOnlyWhenSet()
{
    Project p;
    Tag onlyMax;
    onlyMax.id = 1;
    onlyMax.writeMax = 3.0;
    Tag none;
    none.id = 2;
    p.tags = {onlyMax, none};
    p.nextTagId = 3;

    const QJsonArray tags = QJsonDocument::fromJson(projectToJson(p)).object()
                                .value(QStringLiteral("tags")).toArray();
    const QJsonObject first = tags.at(0).toObject();
    QVERIFY(!first.contains(QStringLiteral("writeMin")));
    QCOMPARE(first.value(QStringLiteral("writeMax")).toDouble(), 3.0);

    const QJsonObject second = tags.at(1).toObject();
    QVERIFY(!second.contains(QStringLiteral("writeMin")));
    QVERIFY(!second.contains(QStringLiteral("writeMax")));
}

void TstProjectIO::writeLimitsNullAndMissingMeanNoLimit()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"writeMin":null,"writeMax":7},{"id":2,"writeMin":-1}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY(!project->tags.at(0).writeMin.has_value());
    QCOMPARE(*project->tags.at(0).writeMax, 7.0);
    QCOMPARE(*project->tags.at(1).writeMin, -1.0);
    QVERIFY(!project->tags.at(1).writeMax.has_value());
}

void TstProjectIO::writeLimitsRejected_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("key");
    QTest::newRow("writeMin string")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"writeMin":"x"}]})")
        << QStringLiteral("writeMin");
    QTest::newRow("writeMax boolean")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"writeMax":true}]})")
        << QStringLiteral("writeMax");
    QTest::newRow("writeMin above writeMax")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"writeMin":5,"writeMax":4}]})")
        << QStringLiteral("writeMin");
}

void TstProjectIO::writeLimitsRejected()
{
    QFETCH(QByteArray, text);
    QFETCH(QString, key);
    QString error;
    QVERIFY(!projectFromJson(text, &error).has_value());
    QVERIFY2(error.contains(key), qPrintable(error));
}

void TstProjectIO::v121KeysDefaultWhenMissing()
{
    // A v1.2 file: no write limits.
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"writable":true,"quickA":1}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    const Tag &tag = project->tags.at(0);
    QVERIFY(!tag.writeMin.has_value());
    QVERIFY(!tag.writeMax.has_value());
    QCOMPARE(*tag.quickA, 1.0);
}

void TstProjectIO::hiddenColumnsDefaultToCompact()
{
    const Project p;
    QCOMPARE(p.hiddenColumns, compactHiddenColumns());
    QVERIFY(!p.hiddenColumns.isEmpty());
}

void TstProjectIO::hiddenColumnsRoundTrip()
{
    Project p = sampleProject();
    p.hiddenColumns = {QStringLiteral("raw"), QStringLiteral("analyze"), QStringLiteral("future")};

    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->hiddenColumns, p.hiddenColumns);
    QCOMPARE(*loaded, p);
}

void TstProjectIO::hiddenColumnsMissingIsCompact()
{
    QString error;
    const auto project = projectFromJson(json(R"({"format":"modbus-control-project","version":1})"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->hiddenColumns, compactHiddenColumns());
}

void TstProjectIO::hiddenColumnsEmptyMeansAllVisible()
{
    Project p = sampleProject();
    p.hiddenColumns.clear();

    const QJsonObject root = QJsonDocument::fromJson(projectToJson(p)).object();
    QVERIFY(root.value(QStringLiteral("hiddenColumns")).isArray());
    QCOMPARE(root.value(QStringLiteral("hiddenColumns")).toArray().size(), qsizetype(0));

    QString error;
    const auto loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QVERIFY(loaded->hiddenColumns.isEmpty());

    const auto fromText = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"hiddenColumns":[]
    })"), &error);
    QVERIFY2(fromText.has_value(), qPrintable(error));
    QVERIFY(fromText->hiddenColumns.isEmpty());
}

void TstProjectIO::hiddenColumnsDuplicatesDropped()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "hiddenColumns":["raw","gain","raw","future","gain"]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->hiddenColumns,
             (QStringList{QStringLiteral("raw"), QStringLiteral("gain"), QStringLiteral("future")}));
}

void TstProjectIO::hiddenColumnsAlwaysWritten()
{
    const QJsonObject root = QJsonDocument::fromJson(projectToJson(Project{})).object();
    QVERIFY(root.value(QStringLiteral("hiddenColumns")).isArray());
    const QJsonArray keys = root.value(QStringLiteral("hiddenColumns")).toArray();
    QCOMPARE(keys.size(), compactHiddenColumns().size());
    for (const QJsonValue &v : keys)
        QVERIFY(v.isString());
}

void TstProjectIO::hiddenColumnsRejected_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::addColumn<QString>("fragment");

    QTest::newRow("not an array")
        << json(R"({"format":"modbus-control-project","version":1,"hiddenColumns":"raw"})")
        << QStringLiteral("hiddenColumns");
    QTest::newRow("number entry")
        << json(R"({"format":"modbus-control-project","version":1,"hiddenColumns":["raw",3]})")
        << QStringLiteral("hiddenColumns");
    QTest::newRow("null entry")
        << json(R"({"format":"modbus-control-project","version":1,"hiddenColumns":[null]})")
        << QStringLiteral("hiddenColumns");
    QTest::newRow("object")
        << json(R"({"format":"modbus-control-project","version":1,"hiddenColumns":{}})")
        << QStringLiteral("hiddenColumns");
}

void TstProjectIO::hiddenColumnsRejected()
{
    QFETCH(QByteArray, text);
    QFETCH(QString, fragment);
    QString error;
    QVERIFY(!projectFromJson(text, &error).has_value());
    QVERIFY2(error.contains(fragment), qPrintable(error));
}

void TstProjectIO::encodingVerifiedRoundTrip()
{
    Project p = sampleProject();
    p.tags[0].encodingVerified = false;
    p.tags[1].encodingVerified = true;

    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, p);
    QVERIFY(!loaded->tags[0].encodingVerified);
    QVERIFY(loaded->tags[1].encodingVerified);
}

void TstProjectIO::encodingVerifiedWrittenOnlyWhenFalse()
{
    Project p;
    Tag unverified;
    unverified.id = 1;
    unverified.encodingVerified = false;
    Tag verified;
    verified.id = 2;
    p.tags = {unverified, verified};
    p.nextTagId = 3;

    const QJsonArray tags = QJsonDocument::fromJson(projectToJson(p)).object()
                                .value(QStringLiteral("tags")).toArray();
    const QJsonObject first = tags.at(0).toObject();
    QVERIFY(first.contains(QStringLiteral("encodingVerified")));
    QCOMPARE(first.value(QStringLiteral("encodingVerified")).toBool(true), false);
    QVERIFY(!tags.at(1).toObject().contains(QStringLiteral("encodingVerified")));
}

void TstProjectIO::encodingVerifiedMissingIsTrue()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1,"type":"float32"},{"id":2,"encodingVerified":false}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY(project->tags.at(0).encodingVerified);
    QVERIFY(!project->tags.at(1).encodingVerified);
}

void TstProjectIO::encodingVerifiedRejected_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::newRow("number")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"encodingVerified":1}]})");
    QTest::newRow("string")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"encodingVerified":"yes"}]})");
    QTest::newRow("null")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"encodingVerified":null}]})");
}

void TstProjectIO::encodingVerifiedRejected()
{
    QFETCH(QByteArray, text);
    QString error;
    QVERIFY(!projectFromJson(text, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("encodingVerified")), qPrintable(error));
}

void TstProjectIO::confirmed16BitRoundTrip()
{
    Project p = sampleProject();
    p.tags[0].confirmed16Bit = true;
    p.tags[1].confirmed16Bit = false;

    QString error;
    const std::optional<Project> loaded = projectFromJson(projectToJson(p), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, p);
    QVERIFY(loaded->tags[0].confirmed16Bit);
    QVERIFY(!loaded->tags[1].confirmed16Bit);
}

void TstProjectIO::confirmed16BitWrittenOnlyWhenTrue()
{
    Project p;
    Tag confirmed;
    confirmed.id = 1;
    confirmed.confirmed16Bit = true;
    Tag plain;
    plain.id = 2;
    p.tags = {confirmed, plain};
    p.nextTagId = 3;

    const QJsonArray tags = QJsonDocument::fromJson(projectToJson(p)).object()
                                .value(QStringLiteral("tags")).toArray();
    const QJsonObject first = tags.at(0).toObject();
    QVERIFY(first.contains(QStringLiteral("confirmed16Bit")));
    QCOMPARE(first.value(QStringLiteral("confirmed16Bit")).toBool(false), true);
    QVERIFY(!tags.at(1).toObject().contains(QStringLiteral("confirmed16Bit")));
}

void TstProjectIO::confirmed16BitMissingIsFalse()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,
        "tags":[{"id":1},{"id":2,"confirmed16Bit":true}]
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QVERIFY(!project->tags.at(0).confirmed16Bit);
    QVERIFY(project->tags.at(1).confirmed16Bit);
}

void TstProjectIO::confirmed16BitRejected_data()
{
    QTest::addColumn<QByteArray>("text");
    QTest::newRow("number")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"confirmed16Bit":1}]})");
    QTest::newRow("string")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"confirmed16Bit":"yes"}]})");
    QTest::newRow("null")
        << json(R"({"format":"modbus-control-project","version":1,"tags":[{"id":1,"confirmed16Bit":null}]})");
}

void TstProjectIO::confirmed16BitRejected()
{
    QFETCH(QByteArray, text);
    QString error;
    QVERIFY(!projectFromJson(text, &error).has_value());
    QVERIFY2(error.contains(QStringLiteral("confirmed16Bit")), qPrintable(error));
}

QTEST_GUILESS_MAIN(TstProjectIO)
#include "tst_projectio.moc"