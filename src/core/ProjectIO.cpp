#include "ProjectIO.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <optional>

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
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1%2\" must be an integer from %3 to %4")
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
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1%2\" must be a number").arg(prefix, key));
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
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1%2\" must be a string").arg(prefix, key));
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
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1%2\" must be true or false").arg(prefix, key));
        return false;
    }
    out = value.toBool();
    return true;
}

// Optional finite number. A missing key or null means "no value" (empty optional).
bool readOptionalNumber(const QJsonObject &obj, const QString &key, const QString &prefix,
                        std::optional<double> &out, QString *error)
{
    const QJsonValue value = obj.value(key);
    if (value.isUndefined() || value.isNull()) {
        out.reset();
        return true;
    }
    if (!value.isDouble() || !std::isfinite(value.toDouble())) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1%2\" must be a number").arg(prefix, key));
        return false;
    }
    out = value.toDouble();
    return true;
}

// JSON [min, max]: an array of two finite numbers. min == max is accepted (a zero-width range is
// what the UI can produce; the caller treats it as "not set").
bool parseSpan(const QJsonValue &value, const QString &name, double &lo, double &hi, QString *error)
{
    if (!value.isArray()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1\" must be an array of 2 numbers [min, max]").arg(name));
        return false;
    }
    const QJsonArray pair = value.toArray();
    if (pair.size() != 2 || !pair.at(0).isDouble() || !pair.at(1).isDouble()
        || !std::isfinite(pair.at(0).toDouble()) || !std::isfinite(pair.at(1).toDouble())) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1\" must be [min, max]: two finite numbers").arg(name));
        return false;
    }
    lo = pair.at(0).toDouble();
    hi = pair.at(1).toDouble();
    return true;
}

// Enum names of the "analysis" keys added in v1.2.6.
QString methodName(ModelMethod method)
{
    switch (method) {
    case ModelMethod::ZeroTo632:
        return QStringLiteral("0-63.2");
    case ModelMethod::Smith:
        return QStringLiteral("smith");
    case ModelMethod::Sundaresan:
        return QStringLiteral("sundaresan");
    case ModelMethod::Tangent:
        return QStringLiteral("tangent");
    case ModelMethod::Custom:
        break;
    }
    return QStringLiteral("custom");
}

QString tuningRuleName(TuningRule rule)
{
    switch (rule) {
    case TuningRule::ZieglerNichols:
        return QStringLiteral("zn");
    case TuningRule::CohenCoon:
        return QStringLiteral("cohen-coon");
    case TuningRule::ImcLambda:
        return QStringLiteral("imc");
    case TuningRule::Simc:
        break;
    }
    return QStringLiteral("simc");
}

QString areaKey(Area area)
{
    switch (area) {
    case Area::HoldingRegister: return QStringLiteral("holding");
    case Area::InputRegister: return QStringLiteral("input");
    case Area::Coil: return QStringLiteral("coil");
    case Area::DiscreteInput: return QStringLiteral("discrete");
    }
    return QStringLiteral("holding");
}

QString axisName(YAxis axis)
{
    return axis == YAxis::Right ? QStringLiteral("right") : QStringLiteral("left");
}

bool readAxis(const QJsonObject &obj, const QString &prefix, YAxis &out, QString *error)
{
    const QString key = QStringLiteral("axis");
    if (!obj.contains(key))
        return true;
    const QString text = obj.value(key).toString();
    if (text == QStringLiteral("left")) {
        out = YAxis::Left;
    } else if (text == QStringLiteral("right")) {
        out = YAxis::Right;
    } else {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1axis\" has unknown value \"%2\" (expected \"left\" or \"right\")")
                            .arg(prefix, text));
        return false;
    }
    return true;
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
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1byteOrder\" has unknown value \"%2\" (expected \"ABCD\", \"CDAB\", \"BADC\" or \"DCBA\")")
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
    } else if (text == QStringLiteral("coil")) {
        out = Area::Coil;
    } else if (text == QStringLiteral("discrete")) {
        out = Area::DiscreteInput;
    } else {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1area\" has unknown value \"%2\" (expected \"holding\", \"input\", \"coil\" or \"discrete\")")
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
    } else if (text == QStringLiteral("bool")) {
        out = ValueType::Bool;
    } else {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1type\" has unknown value \"%2\" (expected \"int16\", \"uint16\", \"int32\", \"uint32\", \"float32\" or \"bool\")")
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
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"connection\" must be an object"));
        return false;
    }
    const QJsonObject obj = value.toObject();
    const QString prefix = QStringLiteral("connection.");

    qint64 port = conn.port;
    qint64 unitId = conn.unitId;
    qint64 timeoutMs = conn.timeoutMs;
    qint64 pollPeriodMs = conn.pollPeriodMs;
    bool legacySingleWrites = false; // obsolete key of older files: validated, then ignored
    // Same ranges as the ConnectionBar spin boxes.
    constexpr qint64 kMinMs = 100;
    constexpr qint64 kMaxMs = 60000;
    if (!readString(obj, QStringLiteral("host"), prefix, conn.host, error)
        || !readInt(obj, QStringLiteral("port"), prefix, 1, 65535, port, error)
        || !readInt(obj, QStringLiteral("unitId"), prefix, 0, 247, unitId, error)
        || !readInt(obj, QStringLiteral("timeoutMs"), prefix, kMinMs, kMaxMs, timeoutMs, error)
        || !readInt(obj, QStringLiteral("pollPeriodMs"), prefix, kMinMs, kMaxMs, pollPeriodMs, error)
        || !readBool(obj, QStringLiteral("singleRegisterWrites"), prefix, legacySingleWrites, error)) {
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
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1\" must be an object").arg(name));
        return false;
    }
    const QJsonObject obj = value.toObject();
    const QString prefix = name + QLatin1Char('.');

    // The id is required: a missing id would fall back to 0, which is never valid.
    const QString idKey = QStringLiteral("id");
    if (!obj.contains(idKey)) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1\" is missing \"id\"").arg(name));
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
        || !readBool(obj, QStringLiteral("writable"), prefix, tag.writable, error)
        || !readBool(obj, QStringLiteral("analyzed"), prefix, tag.analyzed, error)
        || !readBool(obj, QStringLiteral("encodingVerified"), prefix, tag.encodingVerified, error)
        || !readBool(obj, QStringLiteral("confirmed16Bit"), prefix, tag.confirmed16Bit, error)
        || !readAxis(obj, prefix, tag.axis, error)
        || !readOptionalNumber(obj, QStringLiteral("quickA"), prefix, tag.quickA, error)
        || !readOptionalNumber(obj, QStringLiteral("quickB"), prefix, tag.quickB, error)
        || !readOptionalNumber(obj, QStringLiteral("writeMin"), prefix, tag.writeMin, error)
        || !readOptionalNumber(obj, QStringLiteral("writeMax"), prefix, tag.writeMax, error)) {
        return false;
    }
    if (tag.writeMin && tag.writeMax && *tag.writeMin > *tag.writeMax) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1writeMin\" (%2) is above \"%1writeMax\" (%3)")
                            .arg(prefix)
                            .arg(*tag.writeMin)
                            .arg(*tag.writeMax));
        return false;
    }
    tag.address = static_cast<quint16>(address);
    // A bit-area tag without "type" is a bool; bool and bit areas go together.
    if (isBitArea(tag.area) && !obj.contains(QStringLiteral("type")))
        tag.type = ValueType::Bool;
    if (!typeFitsArea(tag.type, tag.area)) {
        setError(error, tag.type == ValueType::Bool
                            ? QCoreApplication::translate("mb::ProjectIO", "Tag %1: type bool needs area coil or discrete")
                                  .arg(tag.id)
                            : QCoreApplication::translate("mb::ProjectIO", "Tag %1: area %2 needs type bool")
                                  .arg(tag.id)
                                  .arg(areaKey(tag.area)));
        return false;
    }
    if (tag.isBit()) {
        // Scaling and byte order do not apply to bits; a discrete input cannot be written.
        tag.gain = 1.0;
        tag.offset = 0.0;
        tag.byteOrder = ByteOrder::ABCD;
        if (tag.area == Area::DiscreteInput)
            tag.writable = false;
    }
    if (!tag.fitsAddressSpace()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1address\" is %2, too high for a \"%3\" tag: it uses %4 registers, "
                                       "so the last one would be past 65535")
                            .arg(prefix)
                            .arg(address)
                            .arg(valueTypeName(tag.type))
                            .arg(registerCount(tag.type)));
        return false;
    }
    return true;
}

// Optional object member `key` of `parent`; `present` says whether it exists. A non-object value is
// an error naming `name` (e.g. "analysis.closedLoop").
bool readSubObject(const QJsonObject &parent, const QString &key, const QString &name,
                   QJsonObject &out, bool &present, QString *error)
{
    const QJsonValue value = parent.value(key);
    present = !value.isUndefined();
    if (!present)
        return true;
    if (!value.isObject()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1\" must be an object").arg(name));
        return false;
    }
    out = value.toObject();
    return true;
}

// A finite number with an inclusive/exclusive range check done by the caller; this only reads it.
bool readFiniteNumber(const QJsonObject &obj, const QString &key, const QString &prefix,
                      double &out, QString *error)
{
    if (!readDouble(obj, key, prefix, out, error))
        return false;
    if (!std::isfinite(out)) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"%1%2\" must be a number").arg(prefix, key));
        return false;
    }
    return true;
}

// The v1.4.0 "analysis" keys (all optional): mode, spTag, closedLoop, controller, compareMethods,
// tangentT632. Missing keys keep the defaults in `out`.
bool readAnalysisV140(const QJsonObject &obj, AnalysisSettings &out, QString *error)
{
    const QString prefix = QStringLiteral("analysis.");

    QString mode;
    if (!readString(obj, QStringLiteral("mode"), prefix, mode, error))
        return false;
    if (obj.contains(QStringLiteral("mode"))) {
        if (mode == QStringLiteral("open")) {
            out.mode = AnalysisMode::OpenLoop;
        } else if (mode == QStringLiteral("closed")) {
            out.mode = AnalysisMode::ClosedLoop;
        } else {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.mode\" has unknown value \"%1\" (expected \"open\" or \"closed\")")
                                .arg(mode));
            return false;
        }
    }

    // Like deltaCoTag, an id that does not exist is kept (the UI treats it as "typed setpoint").
    qint64 spTag = static_cast<qint64>(out.spTagId);
    if (!readInt(obj, QStringLiteral("spTag"), prefix, 0, kMaxTagId, spTag, error))
        return false;
    out.spTagId = static_cast<TagId>(spTag);

    QJsonObject closed;
    bool present = false;
    if (!readSubObject(obj, QStringLiteral("closedLoop"), QStringLiteral("analysis.closedLoop"), closed, present, error))
        return false;
    if (present) {
        const QString cp = QStringLiteral("analysis.closedLoop.");
        ClosedLoopSettings &c = out.closedLoop;
        QString test;
        if (!readString(closed, QStringLiteral("test"), cp, test, error))
            return false;
        if (closed.contains(QStringLiteral("test"))) {
            if (test == QStringLiteral("setpoint")) {
                c.test = ClosedLoopTest::SetpointStep;
            } else if (test == QStringLiteral("disturbance")) {
                c.test = ClosedLoopTest::Disturbance;
            } else {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.closedLoop.test\" has unknown value \"%1\" (expected \"setpoint\" or \"disturbance\")")
                                    .arg(test));
                return false;
            }
        }
        if (!readFiniteNumber(closed, QStringLiteral("spBefore"), cp, c.typedSpBefore, error)
            || !readFiniteNumber(closed, QStringLiteral("spAfter"), cp, c.typedSpAfter, error)) {
            return false;
        }
        if (!readDouble(closed, QStringLiteral("band"), cp, c.settlingBandPct, error))
            return false;
        if (!(c.settlingBandPct > 0.0 && c.settlingBandPct <= 50.0)) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.closedLoop.band\" must be a number between 0 and 50"));
            return false;
        }

        QJsonObject prediction;
        if (!readSubObject(closed, QStringLiteral("prediction"), QStringLiteral("analysis.closedLoop.prediction"),
                           prediction, present, error)) {
            return false;
        }
        if (present) {
            const QString pp = QStringLiteral("analysis.closedLoop.prediction.");
            if (!readBool(prediction, QStringLiteral("show"), pp, c.showPrediction, error)
                || !readFiniteNumber(prediction, QStringLiteral("kp"), pp, c.predictionKp, error)) {
                return false;
            }
            if (!readDouble(prediction, QStringLiteral("ti"), pp, c.predictionTiS, error))
                return false;
            if (!(std::isfinite(c.predictionTiS) && c.predictionTiS >= 0.0)) {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.closedLoop.prediction.ti\" must be a finite number, 0 or more"));
                return false;
            }
            if (!readDouble(prediction, QStringLiteral("td"), pp, c.predictionTdS, error))
                return false;
            if (!(std::isfinite(c.predictionTdS) && c.predictionTdS >= 0.0)) {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.closedLoop.prediction.td\" must be a finite number, 0 or more"));
                return false;
            }
        }
    }

    QJsonObject controller;
    if (!readSubObject(obj, QStringLiteral("controller"), QStringLiteral("analysis.controller"), controller, present, error))
        return false;
    if (present) {
        const QString cp = QStringLiteral("analysis.controller.");
        QString text;
        if (!readString(controller, QStringLiteral("form"), cp, text, error))
            return false;
        if (controller.contains(QStringLiteral("form"))) {
            if (text == QStringLiteral("ideal")) {
                out.controller.form = PidForm::Ideal;
            } else if (text == QStringLiteral("series")) {
                out.controller.form = PidForm::Series;
            } else if (text == QStringLiteral("parallel")) {
                out.controller.form = PidForm::Parallel;
            } else {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.controller.form\" has unknown value \"%1\" (expected \"ideal\", \"series\" or \"parallel\")")
                                    .arg(text));
                return false;
            }
        }
        if (!readString(controller, QStringLiteral("gain"), cp, text, error))
            return false;
        if (controller.contains(QStringLiteral("gain"))) {
            if (text == QStringLiteral("kp")) {
                out.controller.gain = GainUnit::Gain;
            } else if (text == QStringLiteral("pb")) {
                out.controller.gain = GainUnit::ProportionalBand;
            } else {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.controller.gain\" has unknown value \"%1\" (expected \"kp\" or \"pb\")")
                                    .arg(text));
                return false;
            }
        }
        if (!readString(controller, QStringLiteral("integral"), cp, text, error))
            return false;
        if (controller.contains(QStringLiteral("integral"))) {
            if (text == QStringLiteral("time")) {
                out.controller.integral = IntegralUnit::Time;
            } else if (text == QStringLiteral("repeats")) {
                out.controller.integral = IntegralUnit::RepeatsPerMinute;
            } else {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.controller.integral\" has unknown value \"%1\" (expected \"time\" or \"repeats\")")
                                    .arg(text));
                return false;
            }
        }
    }

    const QJsonValue compare = obj.value(QStringLiteral("compareMethods"));
    if (!compare.isUndefined()) {
        if (!compare.isArray()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.compareMethods\" must be an array of method names"));
            return false;
        }
        QList<ModelMethod> methods;
        for (const QJsonValue &entry : compare.toArray()) {
            if (!entry.isString()) {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.compareMethods\" must be an array of method names"));
                return false;
            }
            const QString text = entry.toString();
            ModelMethod method = ModelMethod::Custom;
            if (text == QStringLiteral("0-63.2")) {
                method = ModelMethod::ZeroTo632;
            } else if (text == QStringLiteral("smith")) {
                method = ModelMethod::Smith;
            } else if (text == QStringLiteral("sundaresan")) {
                method = ModelMethod::Sundaresan;
            } else if (text == QStringLiteral("tangent")) {
                method = ModelMethod::Tangent;
            } else {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.compareMethods\" has unknown value \"%1\" (expected \"0-63.2\", \"smith\", \"sundaresan\" or \"tangent\")")
                                    .arg(text));
                return false;
            }
            if (methods.contains(method)) {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.compareMethods\" has the duplicate entry \"%1\"")
                                    .arg(text));
                return false;
            }
            methods.append(method);
        }
        out.compareMethods = methods;
    }

    return readBool(obj, QStringLiteral("tangentT632"), prefix, out.tangentT632, error);
}

// Optional "analysis" object. Every key is optional; a missing key keeps the default in `out`.
// The keys added in v1.2.6 (pvTag, setpointTags, method, ...) fall back to values migrated from
// the per-tag "analyzed" flags (pvTag, setpointTags) or from modelPercents (method) when missing.
bool readAnalysis(const QJsonObject &root, const QList<Tag> &tags, AnalysisSettings &out, QString *error)
{
    const QString key = QStringLiteral("analysis");
    const QJsonValue value = root.value(key);
    QJsonObject obj;
    if (!value.isUndefined()) {
        if (!value.isObject()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis\" must be an object"));
            return false;
        }
        obj = value.toObject();
    }
    const QString prefix = QStringLiteral("analysis.");

    // Before v1.2.6 the analysed tags were flagged per tag: the first one was the PV, all of them
    // were shown as setpoint tags. Used only when the keys are missing (they are read below).
    // deltaCoTag is read first: the migrated PV is the first analysed tag that is not the CO.
    qint64 deltaCoTag = static_cast<qint64>(out.deltaCoTagId);
    if (!readInt(obj, QStringLiteral("deltaCoTag"), prefix, 0, kMaxTagId, deltaCoTag, error))
        return false;
    out.deltaCoTagId = static_cast<TagId>(deltaCoTag);

    out.pvTagId = 0;
    out.setpointTagIds.clear();
    for (const Tag &tag : tags) {
        if (!tag.analyzed)
            continue;
        if (out.pvTagId == 0 && tag.id != out.deltaCoTagId)
            out.pvTagId = tag.id;
        out.setpointTagIds.append(tag.id);
    }
    if (out.pvTagId == 0 && !out.setpointTagIds.isEmpty())
        out.pvTagId = out.setpointTagIds.first();

    const QJsonValue percents = obj.value(QStringLiteral("percents"));
    if (!percents.isUndefined()) {
        if (!percents.isArray()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.percents\" must be an array of numbers"));
            return false;
        }
        const QJsonArray array = percents.toArray();
        if (array.size() > 20) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.percents\" must have at most 20 entries"));
            return false;
        }
        QList<double> list;
        for (const QJsonValue &entry : array) {
            if (!entry.isDouble() || entry.toDouble() < 0.0 || entry.toDouble() > 100.0) {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.percents\" entries must be numbers from 0 to 100"));
                return false;
            }
            list.append(entry.toDouble());
        }
        out.percents = list;
    }

    const QJsonValue reference = obj.value(QStringLiteral("reference"));
    if (!reference.isUndefined()) {
        if (!reference.isString()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.reference\" must be a string"));
            return false;
        }
        const QString text = reference.toString();
        if (text == QStringLiteral("cursor")) {
            out.reference = ReferenceMode::CursorValue;
        } else if (text == QStringLiteral("mean")) {
            out.reference = ReferenceMode::WindowMean;
        } else {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.reference\" has unknown value \"%1\" (expected \"cursor\" or \"mean\")")
                                .arg(text));
            return false;
        }
    }

    if (!readDouble(obj, QStringLiteral("referenceWindowS"), prefix, out.referenceWindowS, error))
        return false;
    if (!(out.referenceWindowS > 0.0 && out.referenceWindowS <= 3600.0)) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.referenceWindowS\" must be greater than 0 and at most 3600"));
        return false;
    }

    if (!readDouble(obj, QStringLiteral("smoothingS"), prefix, out.smoothingS, error))
        return false;
    if (!(out.smoothingS >= 0.0 && out.smoothingS <= 3600.0)) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.smoothingS\" must be from 0 to 3600"));
        return false;
    }

    const QJsonValue model = obj.value(QStringLiteral("modelPercents"));
    if (!model.isUndefined()) {
        const QJsonArray pair = model.toArray();
        if (!model.isArray() || pair.size() != 2 || !pair.at(0).isDouble() || !pair.at(1).isDouble()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.modelPercents\" must be an array of exactly 2 numbers"));
            return false;
        }
        const double a = pair.at(0).toDouble();
        const double b = pair.at(1).toDouble();
        if (!(a >= 0.0 && a < b && b < 100.0)) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.modelPercents\" must be two numbers a < b, both from 0 up to but not including 100"));
            return false;
        }
        out.modelPercentA = a;
        out.modelPercentB = b;
    }

    // Without "method" (files before v1.2.6): 0 / 63.2 % is the two-point method, else Custom.
    // Without an "analysis" object at all the new defaults (Smith) stay.
    if (!value.isUndefined()) {
        out.method = std::fabs(out.modelPercentA) < 1e-9 && std::fabs(out.modelPercentB - 63.2) < 1e-9
                         ? ModelMethod::ZeroTo632
                         : ModelMethod::Custom;
    }
    const QJsonValue method = obj.value(QStringLiteral("method"));
    if (!method.isUndefined()) {
        const QString text = method.toString();
        if (!method.isString()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.method\" must be a string"));
            return false;
        }
        if (text == QStringLiteral("0-63.2")) {
            out.method = ModelMethod::ZeroTo632;
        } else if (text == QStringLiteral("smith")) {
            out.method = ModelMethod::Smith;
        } else if (text == QStringLiteral("sundaresan")) {
            out.method = ModelMethod::Sundaresan;
        } else if (text == QStringLiteral("tangent")) {
            out.method = ModelMethod::Tangent;
        } else if (text == QStringLiteral("custom")) {
            out.method = ModelMethod::Custom;
        } else {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.method\" has unknown value \"%1\" (expected \"0-63.2\", \"smith\", \"sundaresan\", \"tangent\" or \"custom\")")
                                .arg(text));
            return false;
        }
    }

    qint64 pvTag = static_cast<qint64>(out.pvTagId);
    if (!readInt(obj, QStringLiteral("pvTag"), prefix, 0, kMaxTagId, pvTag, error))
        return false;
    out.pvTagId = static_cast<TagId>(pvTag);

    const QJsonValue setpointTags = obj.value(QStringLiteral("setpointTags"));
    if (!setpointTags.isUndefined()) {
        if (!setpointTags.isArray()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.setpointTags\" must be an array of tag ids"));
            return false;
        }
        QList<TagId> ids;
        for (const QJsonValue &entry : setpointTags.toArray()) {
            const double d = entry.toDouble();
            if (!entry.isDouble() || std::trunc(d) != d || d < 1.0 || d > static_cast<double>(kMaxTagId)) {
                setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.setpointTags\" entries must be integers from 1 to %1")
                                    .arg(kMaxTagId));
                return false;
            }
            ids.append(static_cast<TagId>(d));
        }
        out.setpointTagIds = ids;
    }

    // "pvRange" is null (no range) or [min, max]; both are stored, or neither.
    const QJsonValue pvRange = obj.value(QStringLiteral("pvRange"));
    if (pvRange.isNull()) {
        out.pvRangeMin.reset();
        out.pvRangeMax.reset();
    } else if (!pvRange.isUndefined()) {
        double lo = 0.0;
        double hi = 0.0;
        if (!parseSpan(pvRange, QStringLiteral("analysis.pvRange"), lo, hi, error))
            return false;
        if (lo == hi) { // zero-width range = not set
            out.pvRangeMin.reset();
            out.pvRangeMax.reset();
        } else {
            out.pvRangeMin = lo;
            out.pvRangeMax = hi;
        }
    }

    const QJsonValue coRange = obj.value(QStringLiteral("coRange"));
    if (!coRange.isUndefined()
        && !parseSpan(coRange, QStringLiteral("analysis.coRange"), out.coRangeMin, out.coRangeMax, error)) {
        return false;
    }

    if (!readDouble(obj, QStringLiteral("deltaCo"), prefix, out.manualDeltaCo, error))
        return false;
    if (!readBool(obj, QStringLiteral("detailedLabels"), prefix, out.detailedLabels, error))
        return false;
    if (!readBool(obj, QStringLiteral("showModel"), prefix, out.showModel, error))
        return false;
    if (!readBool(obj, QStringLiteral("guideLines"), prefix, out.guideLines, error))
        return false;

    const QJsonValue modelColor = obj.value(QStringLiteral("modelColor"));
    if (!modelColor.isUndefined()) {
        static const QRegularExpression rgb(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
        if (!modelColor.isString() || !rgb.match(modelColor.toString()).hasMatch()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "analysis.modelColor: must be a colour \"#RRGGBB\""));
            return false;
        }
        out.modelColor = modelColor.toString();
    }

    const QJsonValue tuningRule = obj.value(QStringLiteral("tuningRule"));
    if (!tuningRule.isUndefined()) {
        const QString text = tuningRule.toString();
        if (!tuningRule.isString()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.tuningRule\" must be a string"));
            return false;
        }
        if (text == QStringLiteral("zn")) {
            out.tuningRule = TuningRule::ZieglerNichols;
        } else if (text == QStringLiteral("cohen-coon")) {
            out.tuningRule = TuningRule::CohenCoon;
        } else if (text == QStringLiteral("imc")) {
            out.tuningRule = TuningRule::ImcLambda;
        } else if (text == QStringLiteral("simc")) {
            out.tuningRule = TuningRule::Simc;
        } else {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.tuningRule\" has unknown value \"%1\" (expected \"zn\", \"cohen-coon\", \"imc\" or \"simc\")")
                                .arg(text));
            return false;
        }
    }

    if (!readDouble(obj, QStringLiteral("lambdaS"), prefix, out.lambdaS, error))
        return false;
    if (!(std::isfinite(out.lambdaS) && out.lambdaS >= 0.0)) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.lambdaS\" must be a finite number, 0 or more"));
        return false;
    }

    const QJsonValue timeUnit = obj.value(QStringLiteral("timeUnit"));
    if (!timeUnit.isUndefined()) {
        const QString text = timeUnit.toString();
        if (!timeUnit.isString()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.timeUnit\" must be a string"));
            return false;
        }
        if (text == QStringLiteral("s")) {
            out.timeInMinutes = false;
        } else if (text == QStringLiteral("min")) {
            out.timeInMinutes = true;
        } else {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"analysis.timeUnit\" has unknown value \"%1\" (expected \"s\" or \"min\")")
                                .arg(text));
            return false;
        }
    }
    return readAnalysisV140(obj, out, error);
}

// Optional top-level "hiddenColumns": an array of strings. Missing = compact preset. Duplicates
// are dropped (first occurrence kept); unknown keys are kept as they are.
bool readHiddenColumns(const QJsonObject &root, QStringList &out, QString *error)
{
    const QString key = QStringLiteral("hiddenColumns");
    const QJsonValue value = root.value(key);
    if (value.isUndefined()) {
        out = compactHiddenColumns();
        return true;
    }
    if (!value.isArray()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"hiddenColumns\" must be an array of strings"));
        return false;
    }
    QStringList list;
    QSet<QString> seen;
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString()) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "\"hiddenColumns\" entries must be strings"));
            return false;
        }
        const QString name = entry.toString();
        if (!seen.contains(name)) {
            seen.insert(name);
            list.append(name);
        }
    }
    out = list;
    return true;
}

// One side of the optional top-level "yAxes" object: "auto" or [min, max] (two finite numbers,
// min < max). A missing key means "auto". Anything else is rejected.
bool readYRange(const QJsonObject &yAxes, const QString &side, YRange &out, QString *error)
{
    const QJsonValue value = yAxes.value(side);
    if (value.isUndefined() || (value.isString() && value.toString() == QStringLiteral("auto"))) {
        out.reset();
        return true;
    }
    const QString message = QCoreApplication::translate("mb::ProjectIO", "yAxes.%1: must be \"auto\" or [min, max] with min < max").arg(side);
    if (!value.isArray()) {
        setError(error, message);
        return false;
    }
    const QJsonArray pair = value.toArray();
    if (pair.size() != 2 || !pair.at(0).isDouble() || !pair.at(1).isDouble()) {
        setError(error, message);
        return false;
    }
    const double lo = pair.at(0).toDouble();
    const double hi = pair.at(1).toDouble();
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo < hi)) {
        setError(error, message);
        return false;
    }
    out = std::pair<double, double>{lo, hi};
    return true;
}

// Optional top-level "yAxes": an object with the optional keys "left" and "right" (see
// readYRange). Unknown keys are rejected so that a typo does not silently mean "auto".
bool readYAxes(const QJsonObject &root, YRange &left, YRange &right, QString *error)
{
    const QString key = QStringLiteral("yAxes");
    const QJsonValue value = root.value(key);
    if (value.isUndefined()) {
        left.reset();
        right.reset();
        return true;
    }
    if (!value.isObject()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"yAxes\" must be an object with \"left\" and \"right\""));
        return false;
    }
    const QJsonObject obj = value.toObject();
    const QString leftKey = QStringLiteral("left");
    const QString rightKey = QStringLiteral("right");
    for (const QString &name : obj.keys()) {
        if (name != leftKey && name != rightKey) {
            setError(error, QCoreApplication::translate("mb::ProjectIO", "yAxes: unknown key \"%1\" (expected \"left\" or \"right\")").arg(name));
            return false;
        }
    }
    return readYRange(obj, leftKey, left, error) && readYRange(obj, rightKey, right, error);
}

QJsonValue yRangeJson(const YRange &range)
{
    if (!range)
        return QJsonValue(QStringLiteral("auto"));
    return QJsonValue(QJsonArray{range->first, range->second});
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
    };

    QJsonArray tags;
    for (const Tag &tag : project.tags) {
        QJsonObject obj{
            {QStringLiteral("id"), static_cast<qint64>(tag.id)},
            {QStringLiteral("name"), tag.name},
            {QStringLiteral("area"), areaKey(tag.area)},
            {QStringLiteral("address"), static_cast<qint64>(tag.address)},
            {QStringLiteral("type"), valueTypeName(tag.type)},
            {QStringLiteral("unit"), tag.unit},
            {QStringLiteral("color"), tag.color},
            {QStringLiteral("plotted"), tag.plotted},
            {QStringLiteral("writable"), tag.isBit() ? tag.area == Area::Coil && tag.writable : tag.writable},
            {QStringLiteral("axis"), axisName(tag.axis)},
        };
        // Scaling and byte order do not apply to bit tags.
        if (!tag.isBit()) {
            obj.insert(QStringLiteral("byteOrder"), byteOrderName(tag.byteOrder));
            obj.insert(QStringLiteral("gain"), tag.gain);
            obj.insert(QStringLiteral("offset"), tag.offset);
        }
        // Quick values are written only when set.
        if (tag.quickA)
            obj.insert(QStringLiteral("quickA"), *tag.quickA);
        if (tag.quickB)
            obj.insert(QStringLiteral("quickB"), *tag.quickB);
        // Unverified encodings are written only (default true).
        if (!tag.encodingVerified)
            obj.insert(QStringLiteral("encodingVerified"), false);
        // Written only when true (default false).
        if (tag.confirmed16Bit)
            obj.insert(QStringLiteral("confirmed16Bit"), true);
        // Write limits follow the same rule: written only when set.
        if (tag.writeMin)
            obj.insert(QStringLiteral("writeMin"), *tag.writeMin);
        if (tag.writeMax)
            obj.insert(QStringLiteral("writeMax"), *tag.writeMax);
        tags.append(obj);
    }

    const AnalysisSettings &a = project.analysis;
    QJsonArray percents;
    for (double p : a.percents)
        percents.append(p);
    QJsonArray setpointTags;
    for (TagId id : a.setpointTagIds)
        setpointTags.append(static_cast<qint64>(id));
    // pvRange is null unless both ends are set and different (a zero-width range means "not set").
    const QJsonValue pvRange = (a.pvRangeMin && a.pvRangeMax && *a.pvRangeMin != *a.pvRangeMax)
                                   ? QJsonValue(QJsonArray{*a.pvRangeMin, *a.pvRangeMax})
                                   : QJsonValue(QJsonValue::Null);
    QJsonArray compareMethods;
    for (ModelMethod method : a.compareMethods)
        compareMethods.append(methodName(method));
    const QString formName = a.controller.form == PidForm::Series     ? QStringLiteral("series")
                             : a.controller.form == PidForm::Parallel ? QStringLiteral("parallel")
                                                                      : QStringLiteral("ideal");
    const QJsonObject closedLoop{
        {QStringLiteral("test"), a.closedLoop.test == ClosedLoopTest::Disturbance ? QStringLiteral("disturbance")
                                                                                   : QStringLiteral("setpoint")},
        {QStringLiteral("spBefore"), a.closedLoop.typedSpBefore},
        {QStringLiteral("spAfter"), a.closedLoop.typedSpAfter},
        {QStringLiteral("band"), a.closedLoop.settlingBandPct},
        {QStringLiteral("prediction"), QJsonObject{
             {QStringLiteral("show"), a.closedLoop.showPrediction},
             {QStringLiteral("kp"), a.closedLoop.predictionKp},
             {QStringLiteral("ti"), a.closedLoop.predictionTiS},
             {QStringLiteral("td"), a.closedLoop.predictionTdS},
         }},
    };
    const QJsonObject controller{
        {QStringLiteral("form"), formName},
        {QStringLiteral("gain"), a.controller.gain == GainUnit::ProportionalBand ? QStringLiteral("pb")
                                                                                  : QStringLiteral("kp")},
        {QStringLiteral("integral"), a.controller.integral == IntegralUnit::RepeatsPerMinute
                                         ? QStringLiteral("repeats")
                                         : QStringLiteral("time")},
    };
    const QJsonObject analysis{
        {QStringLiteral("mode"), a.mode == AnalysisMode::ClosedLoop ? QStringLiteral("closed") : QStringLiteral("open")},
        {QStringLiteral("spTag"), static_cast<qint64>(a.spTagId)},
        {QStringLiteral("closedLoop"), closedLoop},
        {QStringLiteral("controller"), controller},
        {QStringLiteral("compareMethods"), compareMethods},
        {QStringLiteral("tangentT632"), a.tangentT632},
        {QStringLiteral("percents"), percents},
        {QStringLiteral("reference"),
         a.reference == ReferenceMode::WindowMean ? QStringLiteral("mean") : QStringLiteral("cursor")},
        {QStringLiteral("referenceWindowS"), a.referenceWindowS},
        {QStringLiteral("smoothingS"), a.smoothingS},
        {QStringLiteral("modelPercents"), QJsonArray{a.modelPercentA, a.modelPercentB}},
        {QStringLiteral("method"), methodName(a.method)},
        {QStringLiteral("pvTag"), static_cast<qint64>(a.pvTagId)},
        {QStringLiteral("setpointTags"), setpointTags},
        {QStringLiteral("deltaCoTag"), static_cast<qint64>(a.deltaCoTagId)},
        {QStringLiteral("deltaCo"), a.manualDeltaCo},
        {QStringLiteral("detailedLabels"), a.detailedLabels},
        {QStringLiteral("showModel"), a.showModel},
        {QStringLiteral("modelColor"), a.modelColor},
        {QStringLiteral("guideLines"), a.guideLines},
        {QStringLiteral("pvRange"), pvRange},
        {QStringLiteral("coRange"), QJsonArray{a.coRangeMin, a.coRangeMax}},
        {QStringLiteral("tuningRule"), tuningRuleName(a.tuningRule)},
        {QStringLiteral("lambdaS"), a.lambdaS},
        {QStringLiteral("timeUnit"), a.timeInMinutes ? QStringLiteral("min") : QStringLiteral("s")},
    };

    QJsonArray hiddenColumns;
    for (const QString &key : project.hiddenColumns)
        hiddenColumns.append(key);

    QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("modbus-control-project")},
        {QStringLiteral("version"), static_cast<qint64>(kProjectFormatVersion)},
        {QStringLiteral("connection"), connection},
        {QStringLiteral("addressBase"), project.addressBase == 1 ? 1 : 0},
        {QStringLiteral("nextTagId"), static_cast<qint64>(project.nextTagId)},
        {QStringLiteral("tags"), tags},
        {QStringLiteral("analysis"), analysis},
        {QStringLiteral("hiddenColumns"), hiddenColumns},
        {QStringLiteral("yAxes"), QJsonObject{
             {QStringLiteral("left"), yRangeJson(project.yLeft)},
             {QStringLiteral("right"), yRangeJson(project.yRight)},
         }},
    };
    if (project.locked)
        root.insert(QStringLiteral("locked"), true);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<Project> projectFromJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Invalid JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!doc.isObject()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Invalid project: top-level value must be a JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();

    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("modbus-control-project")) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Not a Modbus Control project: \"format\" must be \"modbus-control-project\""));
        return std::nullopt;
    }

    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (versionValue.isUndefined()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Invalid project: \"version\" is missing"));
        return std::nullopt;
    }
    const double version = versionValue.toDouble();
    if (!versionValue.isDouble() || std::trunc(version) != version) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Invalid project: \"version\" must be an integer"));
        return std::nullopt;
    }
    if (version > kProjectFormatVersion) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Project version %1 is newer than supported version %2")
                            .arg(static_cast<qint64>(version))
                            .arg(kProjectFormatVersion));
        return std::nullopt;
    }
    if (version < 1) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Invalid project: \"version\" must be 1 or greater"));
        return std::nullopt;
    }

    Project project;
    if (!readConnection(root, project.connection, error))
        return std::nullopt;

    qint64 base = 0;
    if (!readInt(root, QStringLiteral("addressBase"), QString(), 0, 1, base, error)) {
        if (error)
            *error = QCoreApplication::translate("mb::ProjectIO", "\"addressBase\" must be 0 or 1");
        return std::nullopt;
    }
    project.addressBase = static_cast<int>(base);

    qint64 storedNextId = 1;
    if (!readInt(root, QStringLiteral("nextTagId"), QString(), 1, kMaxTagId, storedNextId, error))
        return std::nullopt;

    const QJsonValue tagsValue = root.value(QStringLiteral("tags"));
    if (!tagsValue.isUndefined() && !tagsValue.isArray()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"tags\" must be an array"));
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
            setError(error, QCoreApplication::translate("mb::ProjectIO", "Duplicate tag id %1 (tags[%2])")
                                .arg(tag.id)
                                .arg(static_cast<qint64>(i)));
            return std::nullopt;
        }
        seenIds.insert(tag.id);
        maxId = std::max(maxId, tag.id);
        project.tags.append(tag);
    }

    if (!readAnalysis(root, project.tags, project.analysis, error))
        return std::nullopt;

    if (!readHiddenColumns(root, project.hiddenColumns, error))
        return std::nullopt;

    if (!readYAxes(root, project.yLeft, project.yRight, error))
        return std::nullopt;

    const QJsonValue lockedValue = root.value(QStringLiteral("locked"));
    if (!lockedValue.isUndefined() && !lockedValue.isBool()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "\"locked\" must be a boolean"));
        return std::nullopt;
    }
    project.locked = lockedValue.toBool(false);

    project.nextTagId = static_cast<TagId>(std::max<qint64>(storedNextId, static_cast<qint64>(maxId) + 1));
    return project;
}

bool saveProject(const QString &path, const Project &project, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Cannot write \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray json = projectToJson(project);
    if (file.write(json) != json.size()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Cannot write \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    if (!file.commit()) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Cannot save \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

std::optional<Project> loadProject(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Cannot open \"%1\": %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    const QByteArray json = file.readAll();
    if (file.error() != QFile::NoError) {
        setError(error, QCoreApplication::translate("mb::ProjectIO", "Cannot read \"%1\": %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    QString parseError;
    std::optional<Project> project = projectFromJson(json, &parseError);
    if (!project)
        setError(error, QCoreApplication::translate("mb::ProjectIO", "%1: %2").arg(path, parseError));
    return project;
}

QStringList compactHiddenColumns()
{
    return {QStringLiteral("raw"),      QStringLiteral("gain"),     QStringLiteral("offset"),
            QStringLiteral("byteOrder"), QStringLiteral("axis"),     QStringLiteral("color"),
            QStringLiteral("writeMin"), QStringLiteral("writeMax"), QStringLiteral("quickA"),
            QStringLiteral("quickB")};
}

} // namespace mb