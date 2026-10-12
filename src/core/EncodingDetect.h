#pragma once

#include "Tag.h"

#include <QList>
#include <QString>

#include <optional>
#include <utility>

namespace mb {

/// The two registers of a 32-bit tag as the device sent them: r0 at tag.address, r1 at
/// tag.address + 1.
struct RegisterPair {
    quint16 r0 = 0;
    quint16 r1 = 0;
    friend bool operator==(const RegisterPair &, const RegisterPair &) = default;
};

/// Recovers the device registers from a TagSample::raw value (ABCD order, as returned by
/// combineRegisters) and the tag's byte order. Inverse of combineRegisters for 32-bit types:
/// combineRegisters(tag, p.r0, p.r1) == raw for p = deviceRegisters(tag, raw). For 16-bit
/// tags returns {quint16(raw), 0}.
RegisterPair deviceRegisters(const Tag &tag, quint32 raw);

/// A 32-bit encoding: type (Int32, UInt32 or Float32) and byte order.
struct Encoding {
    ValueType type = ValueType::Float32;
    ByteOrder order = ByteOrder::ABCD;
    friend bool operator==(const Encoding &, const Encoding &) = default;
};

/// "float32 CDAB" (toString(type) + ' ' + toString(order)).
QString toString(const Encoding &encoding);
/// The tag's current encoding (meaningful for 32-bit tags only).
Encoding encodingOf(const Tag &tag);
/// Copy of the tag with type and byteOrder replaced (encodingVerified unchanged).
Tag withEncoding(Tag tag, const Encoding &encoding);

enum class EncodingVerdict {
    Clear,       ///< exactly one group is right: the display value matches it, or it is clearly
                 ///< the most plausible
    Ambiguous,   ///< several groups match the display value, or are similarly plausible
    Undecidable, ///< every encoding decodes to the same values (e.g. both registers 0), no readings,
                 ///< or a 16-bit tag: nothing to decide; wait for other readings
    NoMatch,     ///< a display value was given and no encoding matches it
};

/// One of the 12 encodings ({Int32, UInt32, Float32} x {ABCD, CDAB, BADC, DCBA}) applied to the
/// readings.
struct EncodingCandidate {
    Encoding encoding;
    /// Scaled value (decoded * gain + offset) for each reading, in reading order.
    QList<double> values;
    /// Plausibility in [0, 1] over all readings (1 = most plausible, 0 = implausible). Rules:
    /// float32: not finite, |x| > 1e9 or 0 < |x| < 1e-6 (decoded value before scaling) makes a
    /// reading implausible; 1e-3 <= |x| <= 1e6 is good; a short shortest-round-trip decimal
    /// (<= 4 significant digits) adds a small bonus. int32/uint32: |x| <= 1e6 good, up to 1e9
    /// weak, beyond implausible. Across readings: consecutive values whose magnitude changes by
    /// more than a factor of 10 lower the score (to 0 at a factor of 1000): a wrong byte order puts
    /// low-order bits in the exponent or high word. Sign changes and swings around 0 are fine.
    /// A value of exactly 0 is neutral.
    double score = 0.0;
    /// Set only when a display value was given: whether a value of this candidate (the latest
    /// reading) is within the display tolerance.
    std::optional<bool> matchesDisplay;
    /// Index of this candidate's group: candidates that decode to identical values for every
    /// reading share a group. Groups are numbered from 0 = best group.
    int group = 0;
    friend bool operator==(const EncodingCandidate &, const EncodingCandidate &) = default;
};

/// Input besides the tag and readings.
struct EncodingOptions {
    /// Byte order the device is known to use (from its verified tags); tie-breaker only.
    std::optional<ByteOrder> deviceOrder;
    /// Value shown on the device, as typed (empty = none). See parseDisplayValue().
    QString displayValue;
};

struct EncodingResult {
    EncodingVerdict verdict = EncodingVerdict::Undecidable;
    /// All 12 candidates (empty for a 16-bit tag or no readings), sorted by group (best first),
    /// then by preference within the group (see best).
    QList<EncodingCandidate> candidates;
    /// The encoding to use. Clear: the preferred member of the winning group. Ambiguous: the
    /// preferred member of the best-ranked group. Undecidable: the tag's current encoding (if
    /// 32-bit). NoMatch: nullopt. Preference within a group: the tag's current encoding if it is a
    /// member, else a member with options.deviceOrder, else Float32 > Int32 > UInt32, then
    /// ABCD > CDAB > BADC > DCBA.
    std::optional<Encoding> best;
    /// Number of groups (1 when every encoding decodes the same, e.g. registers 0).
    int groupCount = 0;
};

/// Parses a value typed from a device display: optional sign, digits, '.' or ',' as decimal
/// separator, optional exponent; surrounding spaces allowed. Returns (value, tolerance) where
/// tolerance = max(half a unit of the last typed digit, 0.5 % of |value|): "52.3" -> (52.3, 0.2615),
/// "1013" -> (1013, 5.065), "0.05" -> (0.05, 0.005). nullopt if not a finite number.
std::optional<std::pair<double, double>> parseDisplayValue(const QString &text);

/// Ranks the 12 encodings of a 32-bit tag over the readings (oldest first). With a display value,
/// a candidate matches when |latest value - display| <= tolerance; matching groups decide the
/// verdict (one: Clear, several: Ambiguous, none: NoMatch). Without one, the best group is Clear
/// when its score is at least 0.5 and at least 0.25 above the second group's; otherwise
/// Ambiguous. With a single group: Undecidable. options.deviceOrder only breaks ties (equal
/// scores) between groups and within a group.
EncodingResult detectEncoding(const Tag &tag, const QList<RegisterPair> &readings,
                              const EncodingOptions &options = {});

/// For the on-connect check: true when the result is Clear, its best encoding differs from the
/// tag's current one, and the current encoding's candidate scores below 0.5.
bool currentEncodingSuspicious(const Tag &tag, const EncodingResult &result);

} // namespace mb
