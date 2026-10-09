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
    void singleRegisterWritesRoundTrip_data();
    void singleRegisterWritesRoundTrip();
    void singleRegisterWritesDefaultsToFalse();
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
    QVERIFY(conn.value(QStringLiteral("singleRegisterWrites")).isBool());
    QCOMPARE(conn.value(QStringLiteral("singleRegisterWrites")).toBool(), false);

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

void TstProjectIO::singleRegisterWritesRoundTrip_data()
{
    QTest::addColumn<bool>("value");

    QTest::newRow("true") << true;
    QTest::newRow("false") << false;
}

void TstProjectIO::singleRegisterWritesRoundTrip()
{
    QFETCH(bool, value);

    Project p;
    p.connection.singleRegisterWrites = value;
    p.nextTagId = 1;

    const QByteArray data = projectToJson(p);
    const QJsonObject conn = QJsonDocument::fromJson(data).object().value(QStringLiteral("connection")).toObject();
    QCOMPARE(conn.value(QStringLiteral("singleRegisterWrites")).toBool(), value);

    QString error;
    const std::optional<Project> loaded = projectFromJson(data, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->connection.singleRegisterWrites, value);
    QCOMPARE(*loaded, p);
}

void TstProjectIO::singleRegisterWritesDefaultsToFalse()
{
    QString error;
    const auto project = projectFromJson(json(R"({
        "format":"modbus-control-project","version":1,"connection":{"port":502}
    })"), &error);
    QVERIFY2(project.has_value(), qPrintable(error));
    QCOMPARE(project->connection.singleRegisterWrites, false);
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
    QCOMPARE(cm50->connection.singleRegisterWrites, false);
    QCOMPARE(cm50->tags.size(), 6);
    // Register 5009 (as printed in the manual) is protocol address 5008.
    QCOMPARE(cm50->tags.at(0).name, QStringLiteral("Loop 1 Process Variable"));
    QCOMPARE(int(cm50->tags.at(0).address) + cm50->addressBase, 5009);
    QCOMPARE(int(cm50->tags.at(0).address), 5008);
}

void TstProjectIO::analyzedAndAnalysisRoundTrip()
{
    Project original = sampleProject();
    original.tags[0].analyzed = true;
    original.tags[1].analyzed = false;
    original.tags[3].analyzed = true;
    original.analysis.percents = {0.0, 10.0, 63.2, 100.0};
    original.analysis.reference = ReferenceMode::WindowMean;
    original.analysis.referenceWindowS = 2.5;
    original.analysis.smoothingS = 3.0;
    original.analysis.modelPercentA = 10.0;
    original.analysis.modelPercentB = 90.0;
    original.analysis.deltaCoTagId = 2;
    original.analysis.manualDeltaCo = -1.5;
    original.analysis.detailedLabels = true;

    const QByteArray data = projectToJson(original);
    QString error;
    const std::optional<Project> loaded = projectFromJson(data, &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(*loaded, original);
    QVERIFY(loaded->tags[0].analyzed);
    QVERIFY(!loaded->tags[1].analyzed);
    QVERIFY(loaded->tags[3].analyzed);

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
    QCOMPARE(project->analysis, AnalysisSettings{});
    QVERIFY(project->tags.at(0).analyzed);
    QVERIFY(!project->tags.at(1).analyzed);
}

void TstProjectIO::analysisAlwaysWritten()
{
    const QJsonObject root = QJsonDocument::fromJson(projectToJson(Project{})).object();
    QVERIFY(root.value(QStringLiteral("analysis")).isObject());
    const QJsonObject analysis = root.value(QStringLiteral("analysis")).toObject();
    for (const char *key : {"percents", "reference", "referenceWindowS", "smoothingS",
                            "modelPercents", "deltaCoTag", "deltaCo"})
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
    QVERIFY(tag.contains(QStringLiteral("analyzed")));
    QCOMPARE(tag.value(QStringLiteral("analyzed")).toBool(), false);
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

QTEST_GUILESS_MAIN(TstProjectIO)
#include "tst_projectio.moc"