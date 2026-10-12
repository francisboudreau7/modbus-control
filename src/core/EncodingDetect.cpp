#include "EncodingDetect.h"

#include "TagCodec.h"

#include <QRegularExpression>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>

namespace mb {

namespace {

constexpr std::array<ValueType, 3> kTypes{ValueType::Float32, ValueType::Int32, ValueType::UInt32};
constexpr std::array<ByteOrder, 4> kOrders{ByteOrder::ABCD, ByteOrder::CDAB, ByteOrder::BADC,
                                           ByteOrder::DCBA};

quint16 swapBytes(quint16 r)
{
    return static_cast<quint16>((r << 8) | (r >> 8));
}

// ---- Scoring -------------------------------------------------------------------------------
//
// Each reading of a candidate gets a plausibility s in [0,1] from the decoded value x (before
// gain/offset); a decoded value of exactly 0 is neutral (it says nothing and is skipped).
//   float32: not finite, |x| > 1e9 or 0 < |x| < 1e-6 -> 0; 1e-3 <= |x| <= 1e6 -> 0.7, plus 0.25 if
//            the shortest round-trip decimal has <= 4 significant digits; the in-between ranges
//            (1e-6..1e-3, 1e6..1e9) -> 0.25.
//   int32/uint32: |x| <= 1e6 -> 0.6; up to 1e9 -> 0.2; beyond -> 0.
// Ints are deliberately capped below floats: small integers show up in wrong-order float
// reads all the time (the high word of a float is often 0), so a "nice" float must beat them
// by the 0.25 margin required for a Clear verdict.
// The base score is the mean of the non-neutral readings. It is multiplied by a smoothness
// factor: for each pair of consecutive non-neutral readings that differ, the relative jump
// r = |a - b| / max(|a|, |b|) gives f = 1 for r <= 0.25, falling linearly to 0 at r >= 1; the
// factor is the mean of f over those pairs (1 when no pair differs). A wrong word order makes
// the low word dominate and the series jumps about, so it is crushed here.
// A candidate whose readings are all neutral (all zero) scores 0.5.
// A group's score is the best score of its members.

int significantDigits(float f)
{
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof(buf), f, std::chars_format::scientific);
    int digits = 0;
    for (const char *p = buf; p != res.ptr && *p != 'e'; ++p) {
        if (*p >= '0' && *p <= '9')
            ++digits;
    }
    return digits;
}

double readingScore(ValueType type, quint32 raw)
{
    if (type == ValueType::Float32) {
        const float f = std::bit_cast<float>(raw);
        if (!std::isfinite(f))
            return 0.0;
        const double a = std::fabs(static_cast<double>(f));
        if (a > 1e9 || a < 1e-6)
            return 0.0;
        if (a >= 1e-3 && a <= 1e6)
            return significantDigits(f) <= 4 ? 0.95 : 0.7;
        return 0.25;
    }
    const double a = type == ValueType::Int32
                         ? std::fabs(static_cast<double>(static_cast<qint32>(raw)))
                         : static_cast<double>(raw);
    if (a <= 1e6)
        return 0.6;
    if (a <= 1e9)
        return 0.2;
    return 0.0;
}

double smoothness(const QList<double> &v)
{
    double sum = 0.0;
    int n = 0;
    for (qsizetype i = 1; i < v.size(); ++i) {
        const double a = v[i - 1];
        const double b = v[i];
        if (a == 0.0 || b == 0.0 || !std::isfinite(a) || !std::isfinite(b) || a == b)
            continue;
        const double m = std::max(std::fabs(a), std::fabs(b));
        const double r = std::fabs(a - b) / m;
        sum += r <= 0.25 ? 1.0 : std::max(0.0, 1.0 - (r - 0.25) / 0.75);
        ++n;
    }
    return n == 0 ? 1.0 : sum / n;
}

bool sameValue(double a, double b)
{
    if (std::isnan(a) || std::isnan(b))
        return std::isnan(a) && std::isnan(b)
               && std::bit_cast<quint64>(a) == std::bit_cast<quint64>(b);
    return a == b;
}

bool sameValues(const QList<double> &a, const QList<double> &b)
{
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (!sameValue(a[i], b[i]))
            return false;
    }
    return true;
}

int typeRank(ValueType t)
{
    return t == ValueType::Float32 ? 0 : (t == ValueType::Int32 ? 1 : 2);
}

// Preference key within a group (smaller is preferred): the tag's current encoding, then the
// device's byte order, then Float32 > Int32 > UInt32, then ABCD > CDAB > BADC > DCBA.
struct PrefKey {
    int notCurrent;
    int notDevice;
    int type;
    int order;
    auto operator<=>(const PrefKey &) const = default;
};

PrefKey prefKey(const Encoding &e, const Tag &tag, const EncodingOptions &options)
{
    return {e == encodingOf(tag) ? 0 : 1,
            options.deviceOrder && *options.deviceOrder == e.order ? 0 : 1, typeRank(e.type),
            static_cast<int>(e.order)};
}

} // namespace

RegisterPair deviceRegisters(const Tag &tag, quint32 raw)
{
    if (registerCount(tag.type) == 1)
        return {static_cast<quint16>(raw), 0};
    const quint16 ab = static_cast<quint16>(raw >> 16);
    const quint16 cd = static_cast<quint16>(raw & 0xFFFFu);
    switch (tag.byteOrder) {
    case ByteOrder::ABCD:
        return {ab, cd};
    case ByteOrder::CDAB:
        return {cd, ab};
    case ByteOrder::BADC:
        return {swapBytes(ab), swapBytes(cd)};
    case ByteOrder::DCBA:
        return {swapBytes(cd), swapBytes(ab)};
    }
    return {};
}

QString toString(const Encoding &encoding)
{
    return toString(encoding.type) + QLatin1Char(' ') + toString(encoding.order);
}

Encoding encodingOf(const Tag &tag)
{
    return {tag.type, tag.byteOrder};
}

Tag withEncoding(Tag tag, const Encoding &encoding)
{
    tag.type = encoding.type;
    tag.byteOrder = encoding.order;
    return tag;
}

std::optional<std::pair<double, double>> parseDisplayValue(const QString &text)
{
    static const QRegularExpression re(
        QStringLiteral("^[+-]?(?:\\d+(?:[.,](\\d*))?|[.,](\\d+))(?:[eE]([+-]?\\d+))?$"));
    const QString trimmed = text.trimmed();
    const QRegularExpressionMatch m = re.match(trimmed);
    if (!m.hasMatch())
        return std::nullopt;

    QString normalized = trimmed;
    normalized.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double value = normalized.toDouble(&ok);
    if (!ok || !std::isfinite(value))
        return std::nullopt;

    // Digits after the separator: group 1 for "12.5", group 2 for ".5".
    const int decimals = static_cast<int>(m.captured(1).size() + m.captured(2).size());
    const int exponent = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();
    const double half = 0.5 * std::pow(10.0, exponent - decimals);
    const double tolerance = std::max(half, 0.005 * std::fabs(value));
    if (!std::isfinite(tolerance))
        return std::nullopt;
    return std::make_pair(value, tolerance);
}

EncodingResult detectEncoding(const Tag &tag, const QList<RegisterPair> &readings,
                              const EncodingOptions &options)
{
    EncodingResult result;
    const bool is32 = registerCount(tag.type) == 2;
    if (is32)
        result.best = encodingOf(tag);
    if (!is32 || readings.isEmpty())
        return result; // Undecidable, no candidates

    // An unparsable display value is treated as none.
    std::optional<std::pair<double, double>> display;
    if (!options.displayValue.trimmed().isEmpty())
        display = parseDisplayValue(options.displayValue);

    // Build the 12 candidates.
    QList<EncodingCandidate> cands;
    for (ValueType type : kTypes) {
        for (ByteOrder order : kOrders) {
            EncodingCandidate c;
            c.encoding = {type, order};
            const Tag t = withEncoding(tag, c.encoding);
            Tag ut = t; // unscaled twin, for plausibility
            ut.gain = 1.0;
            ut.offset = 0.0;
            QList<double> decoded;
            double sum = 0.0;
            int n = 0;
            for (const RegisterPair &r : readings) {
                const quint32 raw = combineRegisters(t, r.r0, r.r1);
                c.values.append(decodeValue(t, raw));
                const double x = decodeValue(ut, raw);
                decoded.append(x);
                if (x != 0.0) {
                    sum += readingScore(type, raw);
                    ++n;
                }
            }
            c.score = n == 0 ? 0.5 : (sum / n) * smoothness(decoded);
            c.score = std::clamp(c.score, 0.0, 1.0);
            if (display)
                c.matchesDisplay = std::fabs(c.values.last() - display->first) <= display->second;
            cands.append(c);
        }
    }

    // Group candidates with identical values for every reading.
    QList<int> groupOf(cands.size(), -1);
    int groups = 0;
    for (qsizetype i = 0; i < cands.size(); ++i) {
        if (groupOf[i] >= 0)
            continue;
        groupOf[i] = groups;
        for (qsizetype j = i + 1; j < cands.size(); ++j) {
            if (groupOf[j] < 0 && sameValues(cands[i].values, cands[j].values))
                groupOf[j] = groups;
        }
        ++groups;
    }

    struct Group {
        double score = 0.0;
        bool matches = false;
        PrefKey pref{1 << 20, 1 << 20, 0, 0}; // key of the preferred member
    };
    QList<Group> gs(groups);
    for (qsizetype i = 0; i < cands.size(); ++i) {
        Group &g = gs[groupOf[i]];
        g.score = std::max(g.score, cands[i].score);
        g.matches = g.matches || cands[i].matchesDisplay.value_or(false);
        g.pref = std::min(g.pref, prefKey(cands[i].encoding, tag, options));
    }

    // Rank groups: display matches first, then score, then (equal scores) the preference key of
    // each group's preferred member (current encoding, device order, type, byte order).
    QList<int> order(groups);
    for (int i = 0; i < groups; ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const Group &x = gs[a];
        const Group &y = gs[b];
        if (x.matches != y.matches)
            return x.matches;
        if (std::fabs(x.score - y.score) > 1e-9)
            return x.score > y.score;
        return x.pref < y.pref;
    });
    QList<int> rank(groups);
    for (int i = 0; i < groups; ++i)
        rank[order[i]] = i;

    for (qsizetype i = 0; i < cands.size(); ++i)
        cands[i].group = rank[groupOf[i]];
    std::stable_sort(cands.begin(), cands.end(),
                     [&](const EncodingCandidate &a, const EncodingCandidate &b) {
                         if (a.group != b.group)
                             return a.group < b.group;
                         return prefKey(a.encoding, tag, options) < prefKey(b.encoding, tag, options);
                     });

    result.candidates = cands;
    result.groupCount = groups;
    const Encoding topPreferred = cands.first().encoding; // best group, preferred member

    int matchingGroups = 0;
    for (const Group &g : gs)
        matchingGroups += g.matches ? 1 : 0;

    if (groups == 1) {
        // Nothing to decide; but a display value that nothing matches is still worth reporting.
        if (display && matchingGroups == 0) {
            result.verdict = EncodingVerdict::NoMatch;
            result.best = std::nullopt;
        } else {
            result.verdict = EncodingVerdict::Undecidable;
            result.best = encodingOf(tag);
        }
        return result;
    }

    if (display) {
        if (matchingGroups == 0) {
            result.verdict = EncodingVerdict::NoMatch;
            result.best = std::nullopt;
        } else {
            result.verdict = matchingGroups == 1 ? EncodingVerdict::Clear : EncodingVerdict::Ambiguous;
            result.best = topPreferred; // matching groups are ranked first
        }
        return result;
    }

    const double first = gs[order[0]].score;
    const double second = gs[order[1]].score;
    result.verdict = (first >= 0.5 && first >= second + 0.25) ? EncodingVerdict::Clear
                                                              : EncodingVerdict::Ambiguous;
    result.best = topPreferred;
    return result;
}

bool currentEncodingSuspicious(const Tag &tag, const EncodingResult &result)
{
    if (result.verdict != EncodingVerdict::Clear || !result.best)
        return false;
    const Encoding current = encodingOf(tag);
    if (*result.best == current)
        return false;
    for (const EncodingCandidate &c : result.candidates) {
        if (c.encoding == current)
            return c.score < 0.5;
    }
    return false;
}

} // namespace mb
