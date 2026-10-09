#include "ProjectIO.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace mb {
namespace {

// Tag ids are assigned sequentially from 1, so 2^31-1 is far beyond any real project and
// keeps "nextTagId" (max id + 1) inside TagId without overflow.
constexpr qint64 kMaxTagId = 0x7FFFFFFF;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// Optional integer-valued JSON number in [min, max]. A missing key leaves `out` unchanged.
bool readInt(const QJsonObject &obj, const QString &key, const QString &prefix,
             qint64 min, qint64 max, qint64 &out, QString *error)
{
    const QJsonValue value = obj.value(key);
    if (value.isUndefined())
        return true;
    const double d = value.toDouble();
    if (!value.isDouble() || std::trunc(d) != d || d < static_cast<double>(min)
        || d > static_cast<double>(max)) {
        setError(error, QStringLiteral("\"%1%2\" must be an integer from %3 to %4")
                            .arg(prefix, key)
                            .arg(min)
                            .arg(max));
        return false;
    }
    out = static_cast<qint64>(d);
    return true;
}

bool readDouble(const QJsonObject &obj, const QString &key, const QString &prefix,
                double &out, QString *error)
{
    const QJsonValue value = obj.value(key);
    if (value.isUndefined())
        return true;
    if (!value.isDouble()) {
        setError(error, QStringLiteral("\"%1%2\" must be a number").arg(prefix, key));
        return false;
    }
    out = value.toDouble();
    return true;
}

bool readString(const QJsonObject &obj, const QString &key, const QString &prefix,
                QString &out, QString *error)
{
    const QJsonValue value = obj.value(key);
    if (value.isUndefined())
        return true;
    if (!value.isString()) {
        setError(error, QStringLiteral("\"%1%2\" must be a string").arg(prefix, key));
        return false;
    }
    out = value.toString();
    return true;
}

bool readBool(const QJsonObject &obj, const QString &key, const QString &prefix,
              bool &out, QString *error)
{
    const QJsonValue value = obj.value(key);
    if (value.isUndefined())
        return true;
    if (!value.isBool()) {
        setError(error, QStringLiteral("\"%1%2\" must be true or false").arg(prefix, key));
        return false;
    }
    out = value.toBool();
    return true;
}

QString areaName(Area area)
{
    return area == Area::InputRegister ? QStringLiteral("input") : QStringLiteral("holding");
}

QString valueTypeName(ValueType type)
{
    return toString(type);
}

QString byteOrderName(ByteOrder order)
{
    return toString(order);
}

bool readByteOrder(const QJsonObject &obj, const QString &prefix, ByteOrder &out, QString *error)
{
    const QString key = QStringLiteral("byteOrder");
    if (!obj.contains(key))
        return true;
    const QString text = obj.value(key).toString();
    if (text == QStringLiteral("ABCD")) {
        out = ByteOrder::ABCD;
    } else if (text == QStringLiteral("CDAB")) {
        out = ByteOrder::CDAB;
    } else if (text == QStringLiteral("BADC")) {
        out = ByteOrder::BADC;
    } else if (text == QStringLiteral("DCBA")) {
        out = ByteOrder::DCBA;
    } else {
        setError(error, QStringLiteral("\"%1byteOrder\" has unknown value \"%2\" (expected \"ABCD\", \"CDAB\", \"BADC\" or \"DCBA\")")
                            .arg(prefix, text));
        return false;
    }
    return true;
}

bool readArea(const QJsonObject &obj, const QString &prefix, Area &out, QString *error)
{
    const QString key = QStringLiteral("area");
    if (!obj.contains(key))
        return true;
    const QString text = obj.value(key).toString();
    if (text == QStringLiteral("holding")) {
        out = Area::HoldingRegister;
    } else if (text == QStringLiteral("input")) {
        out = Area::InputRegister;
    } else {
        setError(error, QStringLiteral("\"%1area\" has unknown value \"%2\" (expected \"holding\" or \"input\")")
                            .arg(prefix, text));
        return false;
    }
    return true;
}

bool readValueType(const QJsonObject &obj, const QString &prefix, ValueType &out, QString *error)
{
    const QString key = QStringLiteral("type");
    if (!obj.contains(key))
        return true;
    const QString text = obj.value(key).toString();
    if (text == QStringLiteral("int16")) {
        out = ValueType::Int16;
    } else if (text == QStringLiteral("uint16")) {
        out = ValueType::UInt16;
    } else if (text == QStringLiteral("int32")) {
        out = ValueType::Int32;
    } else if (text == QStringLiteral("uint32")) {
        out = ValueType::UInt32;
    } else if (text == QStringLiteral("float32")) {
        out = ValueType::Float32;
    } else {
        setError(error, QStringLiteral("\"%1type\" has unknown value \"%2\" (expected \"int16\", \"uint16\", \"int32\", \"uint32\" or \"float32\")")
                            .arg(prefix, text));
        return false;
    }
    return true;
}

bool readConnection(const QJsonObject &root, ConnectionSettings &conn, QString *error)
{
    const QString key = QStringLiteral("connection");
    const QJsonValue value = root.value(key);
    if (value.isUndefined())
        return true;
    if (!value.isObject()) {
        setError(error, QStringLiteral("\"connection\" must be an object"));
        return false;
    }
    const QJsonObject obj = value.toObject();
    const QString prefix = QStringLiteral("connection.");

    qint64 port = conn.port;
    qint64 unitId = conn.unitId;
    qint64 timeoutMs = conn.timeoutMs;
    qint64 pollPeriodMs = conn.pollPeriodMs;
    // Same ranges as the ConnectionBar spin boxes.
    constexpr qint64 kMinMs = 100;
    constexpr qint64 kMaxMs = 60000;
    if (!readString(obj, QStringLiteral("host"), prefix, conn.host, error)
        || !readInt(obj, QStringLiteral("port"), prefix, 1, 65535, port, error)
        || !readInt(obj, QStringLiteral("unitId"), prefix, 0, 247, unitId, error)
        || !readInt(obj, QStringLiteral("timeoutMs"), prefix, kMinMs, kMaxMs, timeoutMs, error)
        || !readInt(obj, QStringLiteral("pollPeriodMs"), prefix, kMinMs, kMaxMs, pollPeriodMs, error)
        || !readBool(obj, QStringLiteral("singleRegisterWrites"), prefix, conn.singleRegisterWrites, error)) {
        return false;
    }
    conn.port = static_cast<quint16>(port);
    conn.unitId = static_cast<int>(unitId);
    conn.timeoutMs = static_cast<int>(timeoutMs);
    conn.pollPeriodMs = static_cast<int>(pollPeriodMs);
    return true;
}

bool readTag(const QJsonValue &value, int index, Tag &tag, QString *error)
{
    const QString name = QStringLiteral("tags[%1]").arg(index);
    if (!value.isObject()) {
        setError(error, QStringLiteral("\"%1\" must be an object").arg(name));
        return false;
    }
    const QJsonObject obj = value.toObject();
    const QString prefix = name + QLatin1Char('.');

    // The id is required: a missing id would fall back to 0, which is never valid.
    const QString idKey = QStringLiteral("id");
    if (!obj.contains(idKey)) {
        setError(error, QStringLiteral("\"%1\" is missing \"id\"").arg(name));
        return false;
    }
    qint64 id = 0;
    if (!readInt(obj, idKey, prefix, 1, kMaxTagId, id, error))
        return false;
    tag.id = static_cast<TagId>(id);

    qint64 address = tag.address;
    if (!readString(obj, QStringLiteral("name"), prefix, tag.name, error)
        || !readArea(obj, prefix, tag.area, error)
        || !readInt(obj, QStringLiteral("address"), prefix, 0, 65535, address, error)
        || !readValueType(obj, prefix, tag.type, error)
        || !readByteOrder(obj, prefix, tag.byteOrder, error)
        || !readDouble(obj, QStringLiteral("gain"), prefix, tag.gain, error)
        || !readDouble(obj, QStringLiteral("offset"), prefix, tag.offset, error)
        || !readString(obj, QStringLiteral("unit"), prefix, tag.unit, error)
        || !readString(obj, QStringLiteral("color"), prefix, tag.color, error)
        || !readBool(obj, QStringLiteral("plotted"), prefix, tag.plotted, error)
        || !readBool(obj, QStringLiteral("writable"), prefix, tag.writable, error)) {
        return false;
    }
    tag.address = static_cast<quint16>(address);
    if (!tag.fitsAddressSpace()) {
        setError(error, QStringLiteral("\"%1address\" is %2, too high for a \"%3\" tag: it uses %4 registers, "
                                       "so the last one would be past 65535")
                            .arg(prefix)
                            .arg(address)
                            .arg(valueTypeName(tag.type))
                            .arg(registerCount(tag.type)));
        return false;
    }
    return true;
}

} // namespace

QByteArray projectToJson(const Project &project)
{
    const ConnectionSettings &c = project.connection;
    const QJsonObject connection{
        {QStringLiteral("host"), c.host},
        {QStringLiteral("port"), static_cast<qint64>(c.port)},
        {QStringLiteral("unitId"), static_cast<qint64>(c.unitId)},
        {QStringLiteral("timeoutMs"), static_cast<qint64>(c.timeoutMs)},
        {QStringLiteral("pollPeriodMs"), static_cast<qint64>(c.pollPeriodMs)},
        {QStringLiteral("singleRegisterWrites"), c.singleRegisterWrites},
    };

    QJsonArray tags;
    for (const Tag &tag : project.tags) {
        const QJsonObject obj{
            {QStringLiteral("id"), static_cast<qint64>(tag.id)},
            {QStringLiteral("name"), tag.name},
            {QStringLiteral("area"), areaName(tag.area)},
            {QStringLiteral("address"), static_cast<qint64>(tag.address)},
            {QStringLiteral("type"), valueTypeName(tag.type)},
            {QStringLiteral("byteOrder"), byteOrderName(tag.byteOrder)},
            {QStringLiteral("gain"), tag.gain},
            {QStringLiteral("offset"), tag.offset},
            {QStringLiteral("unit"), tag.unit},
            {QStringLiteral("color"), tag.color},
            {QStringLiteral("plotted"), tag.plotted},
            {QStringLiteral("writable"), tag.writable},
        };
        tags.append(obj);
    }

    const QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("modbus-control-project")},
        {QStringLiteral("version"), static_cast<qint64>(kProjectFormatVersion)},
        {QStringLiteral("connection"), connection},
        {QStringLiteral("nextTagId"), static_cast<qint64>(project.nextTagId)},
        {QStringLiteral("tags"), tags},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<Project> projectFromJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!doc.isObject()) {
        setError(error, QStringLiteral("Invalid project: top-level value must be a JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();

    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("modbus-control-project")) {
        setError(error, QStringLiteral("Not a Modbus Control project: \"format\" must be \"modbus-control-project\""));
        return std::nullopt;
    }

    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (versionValue.isUndefined()) {
        setError(error, QStringLiteral("Invalid project: \"version\" is missing"));
        return std::nullopt;
    }
    const double version = versionValue.toDouble();
    if (!versionValue.isDouble() || std::trunc(version) != version) {
        setError(error, QStringLiteral("Invalid project: \"version\" must be an integer"));
        return std::nullopt;
    }
    if (version > kProjectFormatVersion) {
        setError(error, QStringLiteral("Project version %1 is newer than supported version %2")
                            .arg(static_cast<qint64>(version))
                            .arg(kProjectFormatVersion));
        return std::nullopt;
    }
    if (version < 1) {
        setError(error, QStringLiteral("Invalid project: \"version\" must be 1 or greater"));
        return std::nullopt;
    }

    Project project;
    if (!readConnection(root, project.connection, error))
        return std::nullopt;

    qint64 storedNextId = 1;
    if (!readInt(root, QStringLiteral("nextTagId"), QString(), 1, kMaxTagId, storedNextId, error))
        return std::nullopt;

    const QJsonValue tagsValue = root.value(QStringLiteral("tags"));
    if (!tagsValue.isUndefined() && !tagsValue.isArray()) {
        setError(error, QStringLiteral("\"tags\" must be an array"));
        return std::nullopt;
    }
    const QJsonArray tagArray = tagsValue.toArray();
    QSet<TagId> seenIds;
    TagId maxId = 0;
    for (qsizetype i = 0; i < tagArray.size(); ++i) {
        Tag tag;
        if (!readTag(tagArray.at(i), static_cast<int>(i), tag, error))
            return std::nullopt;
        if (seenIds.contains(tag.id)) {
            setError(error, QStringLiteral("Duplicate tag id %1 (tags[%2])")
                                .arg(tag.id)
                                .arg(static_cast<qint64>(i)));
            return std::nullopt;
        }
        seenIds.insert(tag.id);
        maxId = std::max(maxId, tag.id);
        project.tags.append(tag);
    }

    project.nextTagId = static_cast<TagId>(std::max<qint64>(storedNextId, static_cast<qint64>(maxId) + 1));
    return project;
}

bool saveProject(const QString &path, const Project &project, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Cannot write \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray json = projectToJson(project);
    if (file.write(json) != json.size()) {
        setError(error, QStringLiteral("Cannot write \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("Cannot save \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

std::optional<Project> loadProject(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open \"%1\": %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    const QByteArray json = file.readAll();
    if (file.error() != QFile::NoError) {
        setError(error, QStringLiteral("Cannot read \"%1\": %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    QString parseError;
    std::optional<Project> project = projectFromJson(json, &parseError);
    if (!project)
        setError(error, QStringLiteral("%1: %2").arg(path, parseError));
    return project;
}

} // namespace mb