#include "ServerConfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

namespace mbsim {

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

/// Type-checked access to one JSON object. A key that is missing leaves the output
/// untouched. `prefix` is put in front of key names in error messages.
class ObjectReader
{
public:
    ObjectReader(QJsonObject object, QString prefix, QString *error)
        : m_object(std::move(object)), m_prefix(std::move(prefix)), m_error(error)
    {
    }

    std::optional<QJsonValue> get(const char *key) const
    {
        const QString name = QString::fromLatin1(key);
        if (!m_object.contains(name))
            return std::nullopt;
        return m_object.value(name);
    }

    QString *error() const { return m_error; }

    bool failRaw(const QString &message) const
    {
        setError(m_error, message);
        return false;
    }

    bool fail(const char *key, const QString &reason) const
    {
        return failRaw(QStringLiteral("\"%1%2\" %3").arg(m_prefix, QString::fromLatin1(key), reason));
    }

    template <typename T>
    bool readInt(const char *key, T min, T max, T &out) const
    {
        static_assert(std::is_integral_v<T>, "readInt needs an integer type");
        const auto value = get(key);
        if (!value)
            return true;
        if (!value->isDouble())
            return fail(key, QStringLiteral("must be a number"));
        const double number = value->toDouble();
        if (number != std::floor(number) || number < static_cast<double>(min)
            || number > static_cast<double>(max)) {
            return fail(key, QStringLiteral("must be an integer from %1 to %2").arg(min).arg(max));
        }
        out = static_cast<T>(number);
        return true;
    }

    bool readNumber(const char *key, double &out) const
    {
        const auto value = get(key);
        if (!value)
            return true;
        if (!value->isDouble())
            return fail(key, QStringLiteral("must be a number"));
        out = value->toDouble();
        return true;
    }

    bool readInRange(const char *key, double &out, double min, double max) const
    {
        if (!readNumber(key, out))
            return false;
        if (out < min || out > max)
            return fail(key, QStringLiteral("must be between %1 and %2").arg(min).arg(max));
        return true;
    }

    bool readPositive(const char *key, double &out) const
    {
        if (!readNumber(key, out))
            return false;
        if (!(out > 0.0))
            return fail(key, QStringLiteral("must be greater than 0"));
        return true;
    }

    bool readBool(const char *key, bool &out) const
    {
        const auto value = get(key);
        if (!value)
            return true;
        if (!value->isBool())
            return fail(key, QStringLiteral("must be true or false"));
        out = value->toBool();
        return true;
    }

    bool readString(const char *key, QString &out) const
    {
        const auto value = get(key);
        if (!value)
            return true;
        if (!value->isString())
            return fail(key, QStringLiteral("must be a string"));
        out = value->toString();
        return true;
    }

private:
    QJsonObject m_object;
    QString m_prefix;
    QString *m_error;
};

bool readKind(const ObjectReader &r, SignalKind &kind)
{
    struct KindName
    {
        const char *name;
        SignalKind kind;
    };
    static constexpr KindName kNames[] = {
        {"constant", SignalKind::Constant},
        {"sine", SignalKind::Sine},
        {"triangle", SignalKind::Triangle},
        {"sawtooth", SignalKind::Sawtooth},
        {"square", SignalKind::Square},
        {"counter", SignalKind::Counter},
        {"noise", SignalKind::Noise},
        {"lag", SignalKind::Lag},
    };

    const auto value = r.get("kind");
    if (!value)
        return true;
    if (!value->isString())
        return r.fail("kind", QStringLiteral("must be a string"));
    const QString name = value->toString();
    for (const KindName &entry : kNames) {
        if (name == QString::fromLatin1(entry.name)) {
            kind = entry.kind;
            return true;
        }
    }
    return r.fail("kind", QStringLiteral("has unknown kind \"%1\"").arg(name));
}

/// Reads "format". The unsigned formats are accepted only where `allowUnsigned` is set
/// (holding values); signal bindings keep the int16/int32/float32 set.
bool readFormat(const ObjectReader &r, RegisterFormat &format, bool allowUnsigned = false)
{
    const auto value = r.get("format");
    if (!value)
        return true;
    if (!value->isString())
        return r.fail("format", QStringLiteral("must be a string"));
    const QString name = value->toString();
    if (name == QStringLiteral("int16"))
        format = RegisterFormat::Int16;
    else if (name == QStringLiteral("int32"))
        format = RegisterFormat::Int32;
    else if (name == QStringLiteral("float32"))
        format = RegisterFormat::Float32;
    else if (allowUnsigned && name == QStringLiteral("uint16"))
        format = RegisterFormat::UInt16;
    else if (allowUnsigned && name == QStringLiteral("uint32"))
        format = RegisterFormat::UInt32;
    else
        return r.fail("format", QStringLiteral("has unknown format \"%1\"").arg(name));
    return true;
}

QString formatName(RegisterFormat format)
{
    switch (format) {
    case RegisterFormat::Int16:
        return QStringLiteral("int16");
    case RegisterFormat::Int32:
        return QStringLiteral("int32");
    case RegisterFormat::Float32:
        return QStringLiteral("float32");
    case RegisterFormat::UInt16:
        return QStringLiteral("uint16");
    case RegisterFormat::UInt32:
        return QStringLiteral("uint32");
    }
    return QString();
}

bool readByteOrder(const ObjectReader &r, WordOrder &order)
{
    const auto value = r.get("byteOrder");
    if (!value)
        return true;
    if (!value->isString())
        return r.fail("byteOrder", QStringLiteral("must be a string"));
    const QString name = value->toString();
    if (name == QStringLiteral("ABCD"))
        order = WordOrder::ABCD;
    else if (name == QStringLiteral("CDAB"))
        order = WordOrder::CDAB;
    else if (name == QStringLiteral("BADC"))
        order = WordOrder::BADC;
    else if (name == QStringLiteral("DCBA"))
        order = WordOrder::DCBA;
    else
        return r.fail("byteOrder", QStringLiteral("has unknown byteOrder \"%1\"").arg(name));
    return true;
}

bool readBinding(const QJsonObject &object, const QString &prefix, QString *error, SignalBinding &binding)
{
    const ObjectReader r(object, prefix, error);
    SignalParams &params = binding.params;
    return r.readInt<quint16>("input", 0, 65535, binding.inputAddress)
        && readKind(r, params.kind)
        && r.readNumber("amplitude", params.amplitude)
        && r.readNumber("offset", params.offset)
        && r.readPositive("periodS", params.periodS)
        && r.readPositive("tauS", params.tauS)
        && r.readInRange("deadTimeS", params.deadTimeS, 0.0, 60.0)
        && r.readInt<quint16>("source", 0, 65535, params.sourceAddress)
        && readFormat(r, binding.format)
        && readByteOrder(r, binding.byteOrder);
}

/// Checks one holding entry's value against its format's range. Integer formats need whole
/// numbers; float32 needs a finite value that still fits a single-precision float.
bool checkHoldingValue(const ObjectReader &r, const HoldingValue &entry)
{
    if (!std::isfinite(entry.value))
        return r.fail("value", QStringLiteral("must be a finite number"));

    double lo = 0.0;
    double hi = 0.0;
    bool wholeNumber = true;
    switch (entry.format) {
    case RegisterFormat::Int16:
        lo = -32768.0;
        hi = 32767.0;
        break;
    case RegisterFormat::UInt16:
        lo = 0.0;
        hi = 65535.0;
        break;
    case RegisterFormat::Int32:
        lo = -2147483648.0;
        hi = 2147483647.0;
        break;
    case RegisterFormat::UInt32:
        lo = 0.0;
        hi = 4294967295.0;
        break;
    case RegisterFormat::Float32:
        lo = -static_cast<double>(std::numeric_limits<float>::max());
        hi = static_cast<double>(std::numeric_limits<float>::max());
        wholeNumber = false;
        break;
    }
    if (entry.value < lo || entry.value > hi) {
        return r.fail("value", QStringLiteral("must be between %1 and %2 for format \"%3\"")
                                   .arg(QString::number(lo, 'g', 12), QString::number(hi, 'g', 12),
                                        formatName(entry.format)));
    }
    if (wholeNumber && entry.value != std::floor(entry.value))
        return r.fail("value", QStringLiteral("must be a whole number for format \"%1\"").arg(formatName(entry.format)));
    return true;
}

bool readHoldingEntry(const QJsonObject &object, const QString &prefix, QString *error, HoldingValue &entry)
{
    static const QStringList kKeys = {QStringLiteral("address"), QStringLiteral("value"),
                                      QStringLiteral("format"), QStringLiteral("byteOrder")};
    for (const QString &key : object.keys()) {
        if (!kKeys.contains(key)) {
            setError(error, QStringLiteral("\"%1%2\" is not a known key").arg(prefix, key));
            return false;
        }
    }

    const ObjectReader r(object, prefix, error);
    if (!object.contains(QStringLiteral("address")))
        return r.fail("address", QStringLiteral("is required"));
    if (!object.contains(QStringLiteral("value")))
        return r.fail("value", QStringLiteral("is required"));
    return r.readInt<quint16>("address", 0, 65535, entry.address)
        && r.readNumber("value", entry.value)
        && readFormat(r, entry.format, true)
        && readByteOrder(r, entry.byteOrder)
        && checkHoldingValue(r, entry);
}

bool readHolding(const ObjectReader &r, ServerConfig &cfg)
{
    const auto value = r.get("holding");
    if (!value)
        return true;
    if (!value->isArray())
        return r.fail("holding", QStringLiteral("must be an array"));

    const QJsonArray items = value->toArray();
    QList<HoldingValue> entries;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QJsonValue item = items.at(i);
        if (!item.isObject())
            return r.failRaw(QStringLiteral("\"holding[%1]\" must be an object").arg(i));
        HoldingValue entry;
        const QString prefix = QStringLiteral("holding[%1].").arg(i);
        if (!readHoldingEntry(item.toObject(), prefix, r.error(), entry))
            return false;
        entries.append(entry);
    }
    cfg.holdingValues = entries;
    return true;
}

bool readSignals(const ObjectReader &r, ServerConfig &cfg)
{
    const auto value = r.get("signals");
    if (!value)
        return true;
    if (!value->isArray())
        return r.fail("signals", QStringLiteral("must be an array"));

    const QJsonArray items = value->toArray();
    QList<SignalBinding> bindings;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QJsonValue item = items.at(i);
        if (!item.isObject())
            return r.failRaw(QStringLiteral("\"signals[%1]\" must be an object").arg(i));
        SignalBinding binding;
        const QString prefix = QStringLiteral("signals[%1].").arg(i);
        if (!readBinding(item.toObject(), prefix, r.error(), binding))
            return false;
        bindings.append(binding);
    }
    cfg.signalBindings = bindings;
    return true;
}

bool readRoot(const QJsonObject &root, ServerConfig &cfg, QString *error)
{
    const ObjectReader r(root, QString(), error);
    const bool ok = r.readString("host", cfg.host)
        && r.readInt<quint16>("port", 1, 65535, cfg.port)
        && r.readInt("unitId", 0, 247, cfg.unitId)
        && r.readInt("holdingCount", 1, 65536, cfg.holdingCount)
        && r.readInt("inputCount", 1, 65536, cfg.inputCount)
        && r.readInt("tickMs", 1, std::numeric_limits<int>::max(), cfg.tickMs)
        && r.readInt("delayMs", 0, std::numeric_limits<int>::max(), cfg.delayMs)
        && r.readInRange("dropPercent", cfg.dropPercent, 0.0, 100.0)
        && r.readBool("verbose", cfg.verbose)
        && r.readBool("logWrites", cfg.logWrites)
        && r.readBool("strict32Bit", cfg.strict32Bit)
        && readSignals(r, cfg)
        && readHolding(r, cfg);
    if (!ok)
        return false;
    if (cfg.host.isEmpty())
        return r.fail("host", QStringLiteral("must not be empty"));
    return true;
}

/// Checks holding entries against the final holdingCount and against each other.
bool checkHoldingValues(const ServerConfig &cfg, QString *error)
{
    for (qsizetype i = 0; i < cfg.holdingValues.size(); ++i) {
        const HoldingValue &entry = cfg.holdingValues.at(i);
        const int registers = registerCount(entry.format);
        if (entry.address + registers > cfg.holdingCount) {
            setError(error, QStringLiteral("\"holding[%1].address\" is %2, but %3 register(s) from there must fit below holdingCount (%4)")
                                .arg(i)
                                .arg(entry.address)
                                .arg(registers)
                                .arg(cfg.holdingCount));
            return false;
        }
        for (qsizetype j = 0; j < i; ++j) {
            const HoldingValue &other = cfg.holdingValues.at(j);
            const int otherEnd = other.address + registerCount(other.format);
            if (entry.address < otherEnd && other.address < entry.address + registers) {
                setError(error, QStringLiteral("\"holding[%1].address\" overlaps \"holding[%2]\"").arg(i).arg(j));
                return false;
            }
        }
    }
    return true;
}

/// Checks addresses against the final register counts, which are known only after
/// every top-level key has been read.
bool checkBindings(const ServerConfig &cfg, QString *error)
{
    for (qsizetype i = 0; i < cfg.signalBindings.size(); ++i) {
        const SignalBinding &binding = cfg.signalBindings.at(i);
        const int registers = registerCount(binding.format);
        if (binding.inputAddress + registers > cfg.inputCount) {
            setError(error, QStringLiteral("\"signals[%1].input\" is %2, but %3 register(s) from there must fit below inputCount (%4)")
                                .arg(i)
                                .arg(binding.inputAddress)
                                .arg(registers)
                                .arg(cfg.inputCount));
            return false;
        }
        if (binding.params.kind == SignalKind::Lag && binding.params.sourceAddress >= cfg.holdingCount) {
            setError(error, QStringLiteral("\"signals[%1].source\" is %2, but must be below holdingCount (%3)")
                                .arg(i)
                                .arg(binding.params.sourceAddress)
                                .arg(cfg.holdingCount));
            return false;
        }
    }
    return true;
}

} // namespace

ServerConfig ServerConfig::defaults()
{
    ServerConfig cfg;
    cfg.holdingValues = {
        {110, 52.3, RegisterFormat::Float32, WordOrder::CDAB},
        {120, 100.0, RegisterFormat::Int32, WordOrder::ABCD},
    };
    cfg.signalBindings = {
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
    return cfg;
}

std::optional<ServerConfig> parseServerConfig(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        setError(error, QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!document.isObject()) {
        setError(error, QStringLiteral("Invalid JSON: the top-level value must be an object"));
        return std::nullopt;
    }

    ServerConfig config = ServerConfig::defaults();
    if (!readRoot(document.object(), config, error) || !checkBindings(config, error))
        return std::nullopt;
    if (!document.object().contains(QStringLiteral("holding"))) {
        // Files written before "holding" existed may set a small holdingCount; the default
        // initial values that do not fit are dropped instead of failing those files.
        config.holdingValues.removeIf([&config](const HoldingValue &entry) {
            return entry.address + registerCount(entry.format) > config.holdingCount;
        });
    }
    if (!checkHoldingValues(config, error))
        return std::nullopt;
    return config;
}

std::optional<ServerConfig> loadServerConfig(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open \"%1\": %2").arg(path, file.errorString()));
        return std::nullopt;
    }
    return parseServerConfig(file.readAll(), error);
}

} // namespace mbsim
