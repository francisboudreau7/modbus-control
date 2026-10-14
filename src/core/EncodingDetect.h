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
/// separator, optional exponent; surrounding spaces allowed. Text after the last digit (a unit
/// such as "°C" or "%") and spaces between digits are ignored; when both '.' and ',' appear, the
/// one that comes first is a thousands separator ("1,013.25" and "1.013,25" are 1013.25; "1,013"
/// alone is 1.013). Returns (value, tolerance) where
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

/// A float32 value that a 16-bit tag's register is probably one half of.
struct SplitValue {
    quint16 address = 0; ///< first register of the 32-bit value: tag.address - 1 or tag.address
    Encoding encoding;   ///< Float32 with the byte order that gives `value`
    double value = 0.0;  ///< the decoded float (device value: no gain or offset)
    int digits = 0;      ///< significant digits of the value's shortest round-trip decimal form
    friend bool operator==(const SplitValue &, const SplitValue &) = default;
};

/// Whether the register of a 16-bit tag (`own`, read at tag.address) is probably half of a float32
/// value, given its neighbours `below` (tag.address - 1) and `above` (tag.address + 1), each
/// nullopt when it could not be read (or does not exist). For a 32-bit tag returns nullopt.
/// Considers the pairs (below, own) starting at tag.address - 1 and (own, above) starting at
/// tag.address, each decoded as float32 in the word orders ABCD and CDAB only: with the
/// byte-swapped orders (BADC, DCBA) a small integer next to an empty register reads as a clean
/// float (63 and 0 give 0.5), so they would flag ordinary registers; a device that byte-swaps its
/// floats is not checked (documented limitation). A decoding is "clean" when it is finite,
/// 1e-3 <= |x| <= 1e7 and its shortest round-trip decimal form has at most 5 significant digits,
/// or 6 with at most 2 decimals (52.3, 0.5, 100, -17.5, 1013.25 are clean; a full-precision PV
/// such as 23.456789 is not, so it is not detected: documented limitation). When `own` is 0 the
/// result is always nullopt: as the low word of a float a 16-bit write there changes the value by
/// less than 1 %, and an empty register next to a round value is common. Returns the clean decoding with the fewest digits;
/// ties: the pair starting at tag.address, then ABCD > CDAB. nullopt when nothing is clean. Halves
/// of int32 values cannot be told from separate 16-bit registers and are not considered.
std::optional<SplitValue> detectSplitValue(const Tag &tag, quint16 own, std::optional<quint16> below,
                                           std::optional<quint16> above);

/// What the Add tag dialog read around a register `address` before the tag exists.
struct ProbeReadings {
    /// The register alone, read with one single-register request; nullopt when that read failed.
    std::optional<quint16> own;
    /// The single-register read of `address` was answered with exception 2 (illegal data address),
    /// as devices such as the ABB CM50 do for one half of a 32-bit value.
    bool ownRefused = false;
    /// Neighbours, each read alone (address - 1 and address + 1); nullopt when not read, refused,
    /// or outside 0..65535. Only read when `own` was.
    std::optional<quint16> below;
    std::optional<quint16> above;
    /// False when a neighbour that could be part of the same value was not read (its read failed
    /// or was refused). A neighbour outside 0..65535 or used by another tag of the project is
    /// left out (nullopt) but does not make this false: it cannot be the other half.
    bool neighboursComplete = true;
    /// Only read when ownRefused: the pairs starting at address and at address - 1 (two-register
    /// reads); nullopt when refused, failed or outside 0..65535.
    std::optional<RegisterPair> pairAt;
    std::optional<RegisterPair> pairBelow;
};

enum class LayoutKind {
    Sixteen,    ///< a 16-bit value at `address`
    ThirtyTwo,  ///< a 32-bit value starting at LayoutSuggestion::start (address or address - 1)
    Unknown,    ///< nothing to decide from these readings (see LayoutReason)
};

enum class LayoutReason {
    NoFloatAround,  ///< Sixteen: own read alone, no clean float32 with a neighbour
    NeighboursUnread, ///< Unknown: own read alone, no clean float32 with the neighbours that were
                      ///< read, but a neighbour could not be read (neighboursComplete false)
    SplitFound,     ///< ThirtyTwo: own read alone, but it is half of a clean float32 (detectSplitValue)
    Refused,        ///< ThirtyTwo: own refused alone, a pair including it reads
    OwnZero,        ///< Unknown: own reads 0 (can't tell 16 from 32-bit)
    RefusedNoPair,  ///< Unknown: own refused and neither pair reads
    NotRead,        ///< Unknown: own could not be read (an error other than exception 2)
};

/// Suggested layout for a new tag at (area, address).
struct LayoutSuggestion {
    LayoutKind kind = LayoutKind::Unknown;
    LayoutReason reason = LayoutReason::NotRead;
    /// First register of the suggested tag: `address`, or address - 1 for a 32-bit value that
    /// starts one register earlier.
    quint16 start = 0;
    /// ThirtyTwo only: the registers at start and start + 1.
    RegisterPair pair;
    /// ThirtyTwo only: detectEncoding over {pair} for a Float32 tag at `start` (gain 1, offset 0,
    /// byte order deviceOrder or ABCD; for SplitFound the split's order), options {deviceOrder}.
    EncodingResult ranking;
    /// SplitFound only: the clean float found by detectSplitValue.
    std::optional<SplitValue> split;
};

/// Decides, from the probe reads, whether a new tag at `address` holds a 16-bit value or (part of)
/// a 32-bit value. Rules, in order:
/// - own read: own == 0 -> Unknown/OwnZero. Else detectSplitValue(<Int16 tag at address>, own,
///   below, above): a split -> ThirtyTwo/SplitFound, start = split.address, pair from the two
///   registers; none -> Sixteen/NoFloatAround when readings.neighboursComplete, else
///   Unknown/NeighboursUnread; start = address.
/// - ownRefused: of pairAt and pairBelow, those present are ranked with detectEncoding as above;
///   the chosen pair is the only one, or the one whose best group scores higher (tie: pairAt) ->
///   ThirtyTwo/Refused. Neither present -> Unknown/RefusedNoPair.
/// - otherwise -> Unknown/NotRead.
/// `area` is copied into the tags used for ranking; it does not change the decision.
LayoutSuggestion suggestLayout(Area area, quint16 address, const ProbeReadings &readings,
                               std::optional<ByteOrder> deviceOrder = {});

} // namespace mb
