#include "Scenario.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QStringList>

#include <cmath>

namespace mbloop {
namespace {

constexpr qint64 kScenarioVersion = 1;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

// Rejects any key not in `allowed`. `prefix` is "" at the top level and "settings." etc. below.
bool checkKeys(const QJsonObject &obj, const QStringList &allowed, const QString &prefix,
               QString *error)
{
    for (const QString &key : obj.keys()) {
        if (!allowed.contains(key)) {
            setError(error, QStringLiteral("%1%2: unknown key").arg(prefix, key));
            return false;
        }
    }
    return true;
}

// Optional JSON object member. A missing key is not an error; `out` is set to the object if present.
bool readObject(const QJsonObject &obj, const QString &key, QJsonObject &out, bool &present,
                QString *error)
{
    const QJsonValue value = obj.value(key);
    present = !value.isUndefined();
    if (!present)
        return true;
    if (!value.isObject()) {
        setError(error, QStringLiteral("%1: must be an object").arg(key));
        return false;
    }
    out = value.toObject();
    return true;
}

// Optional number in [min, max]. A missing key leaves `out` unchanged.
bool readRange(const QJsonObject &obj, const QString &key, const QString &prefix, double min,
               double max, double &out, QString *error)
{
    const QJsonValue value = obj.value(key);
    if (value.isUndefined())
        return true;
    const QString name = prefix + key;
    if (!value.isDouble()) {
        setError(error, QStringLiteral("%1: must be a number").arg(name));
        return false;
    }
    const double d = value.toDouble();
    if (d < min || d > max) {
        setError(error, QStringLiteral("%1: must be %2..%3")
                            .arg(name, QString::number(min), QString::number(max)));
        return false;
    }
    out = d;
    return true;
}

bool readSpeedFactor(const QJsonObject &obj, const QString &prefix, double &out, QString *error)
{
    const QJsonValue value = obj.value(QStringLiteral("speedFactor"));
    if (value.isUndefined())
        return true;
    const QString name = prefix + QStringLiteral("speedFactor");
    if (!value.isDouble()) {
        setError(error, QStringLiteral("%1: must be a number").arg(name));
        return false;
    }
    const double d = value.toDouble();
    if (d != 1.0 && d != 2.0 && d != 5.0) {
        setError(error, QStringLiteral("%1: must be 1, 2 or 5").arg(name));
        return false;
    }
    out = d;
    return true;
}

bool readSettings(const QJsonObject &root, Scenario &scenario, QString *error)
{
    QJsonObject obj;
    bool present = false;
    if (!readObject(root, QStringLiteral("settings"), obj, present, error))
        return false;
    if (!present)
        return true;
    const QString prefix = QStringLiteral("settings.");
    if (!checkKeys(obj,
                   {QStringLiteral("pumpSpeedPct"), QStringLiteral("hvOpeningPct"),
                    QStringLiteral("deadTimeS"), QStringLiteral("ltNoisePct"),
                    QStringLiteral("ltDampingS"), QStringLiteral("fcvStrokeS"),
                    QStringLiteral("speedFactor")},
                   prefix, error)) {
        return false;
    }
    LoopParams &p = scenario.params;
    if (!readRange(obj, QStringLiteral("pumpSpeedPct"), prefix, 0, 100, p.pumpSpeedPct, error)
        || !readRange(obj, QStringLiteral("hvOpeningPct"), prefix, 0, 100, p.hvOpeningPct, error)
        || !readRange(obj, QStringLiteral("deadTimeS"), prefix, 0, 30, p.deadTimeS, error)
        || !readRange(obj, QStringLiteral("ltNoisePct"), prefix, 0, 5, p.ltNoisePct, error)
        || !readRange(obj, QStringLiteral("ltDampingS"), prefix, 0, 60, p.ltDampingS, error)
        || !readRange(obj, QStringLiteral("fcvStrokeS"), prefix, 0, 120, p.fcvStrokeS, error)
        || !readSpeedFactor(obj, prefix, scenario.speedFactor, error)) {
        return false;
    }
    return true;
}

bool readState(const QJsonObject &root, Scenario &scenario, QString *error)
{
    QJsonObject obj;
    bool present = false;
    if (!readObject(root, QStringLiteral("state"), obj, present, error))
        return false;
    if (!present)
        return true;
    const QString prefix = QStringLiteral("state.");
    if (!checkKeys(obj,
                   {QStringLiteral("levelPct"), QStringLiteral("outPct"),
                    QStringLiteral("spPct"), QStringLiteral("stemPct")},
                   prefix, error)) {
        return false;
    }
    Scenario::State state;
    if (!readRange(obj, QStringLiteral("levelPct"), prefix, 0, 99, state.levelPct, error)
        || !readRange(obj, QStringLiteral("outPct"), prefix, 0, 100, state.outPct, error)
        || !readRange(obj, QStringLiteral("spPct"), prefix, 0, 100, state.spPct, error)
        || !readRange(obj, QStringLiteral("stemPct"), prefix, 0, 100, state.stemPct, error)) {
        return false;
    }
    scenario.state = state;
    return true;
}

} // namespace

QByteArray scenarioToJson(const Scenario &scenario)
{
    const LoopParams &p = scenario.params;
    QJsonObject settings{
        {QStringLiteral("pumpSpeedPct"), p.pumpSpeedPct},
        {QStringLiteral("hvOpeningPct"), p.hvOpeningPct},
        {QStringLiteral("deadTimeS"), p.deadTimeS},
        {QStringLiteral("ltNoisePct"), p.ltNoisePct},
        {QStringLiteral("ltDampingS"), p.ltDampingS},
        {QStringLiteral("fcvStrokeS"), p.fcvStrokeS},
        {QStringLiteral("speedFactor"), scenario.speedFactor},
    };
    QJsonObject root{
        {QStringLiteral("format"), QStringLiteral("modbus-loop-scenario")},
        {QStringLiteral("version"), static_cast<qint64>(kScenarioVersion)},
        {QStringLiteral("settings"), settings},
    };
    if (scenario.state) {
        const Scenario::State &s = *scenario.state;
        root.insert(QStringLiteral("state"), QJsonObject{
                                                 {QStringLiteral("levelPct"), s.levelPct},
                                                 {QStringLiteral("outPct"), s.outPct},
                                                 {QStringLiteral("spPct"), s.spPct},
                                                 {QStringLiteral("stemPct"), s.stemPct},
                                             });
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<Scenario> scenarioFromJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!doc.isObject()) {
        setError(error, QStringLiteral("Invalid scenario: top-level value must be a JSON object"));
        return std::nullopt;
    }
    const QJsonObject root = doc.object();

    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("modbus-loop-scenario")) {
        setError(error, QStringLiteral("Not a loop scenario: \"format\" must be \"modbus-loop-scenario\""));
        return std::nullopt;
    }
    const QJsonValue versionValue = root.value(QStringLiteral("version"));
    if (versionValue.isUndefined()) {
        setError(error, QStringLiteral("Invalid scenario: \"version\" is missing"));
        return std::nullopt;
    }
    const double version = versionValue.toDouble();
    if (!versionValue.isDouble() || std::trunc(version) != version) {
        setError(error, QStringLiteral("Invalid scenario: \"version\" must be an integer"));
        return std::nullopt;
    }
    if (version != static_cast<double>(kScenarioVersion)) {
        setError(error, QStringLiteral("Unsupported scenario version %1 (expected %2)")
                            .arg(QString::number(version, 'f', 0))
                            .arg(kScenarioVersion));
        return std::nullopt;
    }
    if (!checkKeys(root,
                   {QStringLiteral("format"), QStringLiteral("version"),
                    QStringLiteral("settings"), QStringLiteral("state")},
                   QString(), error)) {
        return std::nullopt;
    }

    Scenario scenario;
    if (!readSettings(root, scenario, error) || !readState(root, scenario, error))
        return std::nullopt;
    return scenario;
}

bool saveScenario(const QString &path, const Scenario &scenario, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("Cannot write \"%1\": %2").arg(path, file.errorString()));
        return false;
    }
    const QByteArray json = scenarioToJson(scenario);
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

std::optional<Scenario> loadScenario(const QString &path, QString *error)
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
    std::optional<Scenario> scenario = scenarioFromJson(json, &parseError);
    if (!scenario)
        setError(error, QStringLiteral("%1: %2").arg(path, parseError));
    return scenario;
}

void applyScenario(LoopModel &model, const Scenario &scenario)
{
    model.setParams(scenario.params);
    model.reset(scenario.params, 50.0);
    if (scenario.state) {
        const Scenario::State &s = *scenario.state;
        model.setLevelM(s.levelPct / 100.0 * loop101::kTankHeightM);
        model.setOut(s.outPct);
        model.setSp(s.spPct);
        model.setStemPct(s.stemPct);
        model.fillPipe();
    }
}

} // namespace mbloop
