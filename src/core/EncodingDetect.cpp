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
// factor: for each pair of consecutive non-neutral readings that differ, the change of
// magnitude q = |log10(|b| / |a|)| gives f = 1 for q <= 1 (within a factor of 10), falling
// linearly to 0 at q >= 3 (a factor of 1000); the factor is the mean of f over those pairs (1 when
// no pair differs). A wrong byte order puts low-order bits in the exponent or the high word, so
// its magnitude jumps by orders of magnitude and it is crushed here. A real signal may change
// sign or swing widely around zero (5 -> -10 is a 150 % change), so relative change is not used.
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
        const double q = std::fabs(std::log10(std::fabs(b) / std::fabs(a)));
        sum += q <= 1.0 ? 1.0 : std::max(0.0, 1.0 - (q - 1.0) / 2.0);
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
    QString trimmed = text.trimmed();
    // Drop what follows the last digit (a unit), then spaces between digits ("1 013,25").
    qsizetype last = trimmed.size() - 1;
    while (last >= 0 && !trimmed.at(last).isDigit())
        --last;
    // A suffix that continues the number ("1e", "5.", "3-") is an incomplete number, not a unit.
    const QString suffix = trimmed.mid(last + 1).trimmed();
    if (!suffix.isEmpty() && QStringLiteral("eE+-.,").contains(suffix.at(0)))
        return std::nullopt;
    trimmed = trimmed.left(last + 1);
    trimmed.remove(QLatin1Char(' '));
    trimmed.remove(QChar(0x00A0));
    trimmed.remove(QChar(0x202F));
    // Both separators: the first one is for thousands.
    const qsizetype dot = trimmed.indexOf(QLatin1Char('.'));
    const qsizetype comma = trimmed.indexOf(QLatin1Char(','));
    if (dot >= 0 && comma >= 0)
        trimmed.remove(comma < dot ? QLatin1Char(',') : QLatin1Char('.'));
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

// A "clean" float for the split check: at most 5 significant digits, or 6 with at most 2
// decimals (1013.25). Measured on separate int16 neighbours (word orders only): 0 % for small or
// signed values (and for 1..1000 next to an empty register), about 0.5 % for Siemens-style raw
// values 0..27648 (6 digits with any decimals: 1 %; all four orders: up to 8.6 %). Any int16 value
// next to an empty register is the weak spot: about 7 % of them read as a clean float (17000 next
// to 0 reads as 58.0); the user answers Keep once and the app remembers it.
bool cleanForSplit(float f, int digits)
{
    if (digits <= 5)
        return true;
    if (digits > 6)
        return false;
    // Decimals of the shortest form: digits after the decimal point = digits - 1 - exponent.
    const int exponent = static_cast<int>(std::floor(std::log10(std::fabs(static_cast<double>(f)))));
    return digits - 1 - exponent <= 2;
}

std::optional<SplitValue> detectSplitValue(const Tag &tag, quint16 own,
                                           std::optional<quint16> below,
                                           std::optional<quint16> above)
{
    if (registerCount(tag.type) != 1 || own == 0)
        return std::nullopt;

    // Pairs in preference order: the one starting at tag.address first.
    struct Pair {
        quint16 address;
        quint16 r0;
        quint16 r1;
    };
    std::array<Pair, 2> pairs{};
    int n = 0;
    if (above && tag.address < 0xFFFF)
        pairs[n++] = {tag.address, own, *above};
    if (below && tag.address > 0)
        pairs[n++] = {static_cast<quint16>(tag.address - 1), *below, own};

    std::optional<SplitValue> best;
    for (int i = 0; i < n; ++i) {
        const Pair &p = pairs[i];
        if (p.r0 == 0 && p.r1 == 0)
            continue;
        for (ByteOrder order : {ByteOrder::ABCD, ByteOrder::CDAB}) {
            Tag t;
            t.type = ValueType::Float32;
            t.byteOrder = order;
            const float f = std::bit_cast<float>(combineRegisters(t, p.r0, p.r1));
            if (!std::isfinite(f))
                continue;
            const double a = std::fabs(static_cast<double>(f));
            if (a < 1e-3 || a > 1e7)
                continue;
            const int digits = significantDigits(f);
            if (!cleanForSplit(f, digits))
                continue;
            if (!best || digits < best->digits)
                best = SplitValue{p.address, {ValueType::Float32, order}, static_cast<double>(f),
                                  digits};
        }
    }
    return best;
}

} // namespace mb

namespace mb {

namespace {

EncodingResult rankPair(Area area, quint16 start, const RegisterPair &pair, ByteOrder order,
                        std::optional<ByteOrder> deviceOrder)
{
    Tag t;
    t.area = area;
    t.address = start;
    t.type = ValueType::Float32;
    t.byteOrder = order;
    t.gain = 1.0;
    t.offset = 0.0;
    EncodingOptions options;
    options.deviceOrder = deviceOrder;
    return detectEncoding(t, {pair}, options);
}

double bestGroupScore(const EncodingResult &r)
{
    return r.candidates.isEmpty() ? 0.0 : r.candidates.first().score;
}

} // namespace

LayoutSuggestion suggestLayout(Area area, quint16 address, const ProbeReadings &readings,
                               std::optional<ByteOrder> deviceOrder)
{
    LayoutSuggestion s;
    s.start = address;

    if (readings.own) {
        if (*readings.own == 0) {
            s.kind = LayoutKind::Unknown;
            s.reason = LayoutReason::OwnZero;
            return s;
        }
        Tag t;
        t.area = area;
        t.address = address;
        t.type = ValueType::Int16;
        const auto split = detectSplitValue(t, *readings.own, readings.below, readings.above);
        if (!split) {
            s.kind = readings.neighboursComplete ? LayoutKind::Sixteen : LayoutKind::Unknown;
            s.reason = readings.neighboursComplete ? LayoutReason::NoFloatAround
                                                   : LayoutReason::NeighboursUnread;
            return s;
        }
        s.kind = LayoutKind::ThirtyTwo;
        s.reason = LayoutReason::SplitFound;
        s.start = split->address;
        s.split = split;
        if (split->address == address)
            s.pair = {*readings.own, readings.above.value_or(0)};
        else
            s.pair = {readings.below.value_or(0), *readings.own};
        s.ranking = rankPair(area, s.start, s.pair, split->encoding.order, deviceOrder);
        return s;
    }

    if (readings.ownRefused) {
        const ByteOrder order = deviceOrder.value_or(ByteOrder::ABCD);
        std::optional<EncodingResult> rankAt;
        std::optional<EncodingResult> rankBelow;
        if (readings.pairAt)
            rankAt = rankPair(area, address, *readings.pairAt, order, deviceOrder);
        if (readings.pairBelow && address > 0)
            rankBelow = rankPair(area, static_cast<quint16>(address - 1), *readings.pairBelow,
                                 order, deviceOrder);
        if (!rankAt && !rankBelow) {
            s.kind = LayoutKind::Unknown;
            s.reason = LayoutReason::RefusedNoPair;
            return s;
        }
        const bool useBelow =
            rankBelow && (!rankAt || bestGroupScore(*rankBelow) > bestGroupScore(*rankAt));
        s.kind = LayoutKind::ThirtyTwo;
        s.reason = LayoutReason::Refused;
        if (useBelow) {
            s.start = static_cast<quint16>(address - 1);
            s.pair = *readings.pairBelow;
            s.ranking = *rankBelow;
        } else {
            s.start = address;
            s.pair = *readings.pairAt;
            s.ranking = *rankAt;
        }
        return s;
    }

    s.kind = LayoutKind::Unknown;
    s.reason = LayoutReason::NotRead;
    return s;
}

} // namespace mb
