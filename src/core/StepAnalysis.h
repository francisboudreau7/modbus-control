#pragma once

#include "Recorder.h"
#include "Tag.h"

#include <QList>
#include <QString>

#include <limits>
#include <optional>

namespace mb {

/// How the 0 % (y0) and 100 % (y100) levels of a step response are taken.
enum class ReferenceMode : quint8 {
    CursorValue, ///< Recorder::sampleAt(tag, cursor time)
    WindowMean,  ///< mean of the non-NaN samples with time in [cursor - W, cursor]
};

/// User settings of the Analysis tab (saved in the project, see ProjectIO.h).
struct AnalysisSettings {
    /// Requested setpoints in percent, each in [0, 100], at most 20 entries. The UI keeps
    /// them sorted ascending without duplicates; the analysis keeps the given order.
    QList<double> percents{0.0, 63.2};
    ReferenceMode reference = ReferenceMode::CursorValue;
    double referenceWindowS = 1.0; ///< W for WindowMean, in (0, 3600]
    /// S: centred moving-average width in [0, 3600]; 0 = off. A centred average starts
    /// rising up to S/2 before the real response, so low percentages (0 %) come out early by
    /// up to S/2 and L is biased low; with smoothing, prefer a model "From %" above 0 (e.g. 10)
    /// or keep S small compared with L.
    double smoothingS = 0.0;
    /// Setpoints a < b used for the model, both in [0, 100).
    double modelPercentA = 0.0;
    double modelPercentB = 63.2;
    /// Controller-output tag whose change between the cursors is ΔCO. 0 = use manualDeltaCo.
    /// An id that is not in the project or not recorded is treated like "no ΔCO".
    TagId deltaCoTagId = 0;
    /// ΔCO typed by the user (in the CO's units) when deltaCoTagId == 0. 0 = unknown (no K).
    double manualDeltaCo = 0.0;
    /// Plot labels of the setpoint markers: false = "63.2 %"; true = three lines
    /// "63.2 %", "t = 14.360 s", "y = 22.64 degC" (target value and tag unit).
    bool detailedLabels = false;

    friend bool operator==(const AnalysisSettings &, const AnalysisSettings &) = default;
};

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

/// One requested percentage of one tag.
struct SetpointResult {
    double percent = 0.0;
    double target = kNaN;   ///< y0 + percent/100 * (y100 - y0)
    bool found = false;
    double time = kNaN;     ///< seconds in the Recorder's time base (NaN if !found)
    /// Recorded (unsmoothed) value at `time`, linearly interpolated between the two
    /// neighbouring non-NaN samples (NaN if !found).
    double measured = kNaN;
};

/// Analysis of one process-variable tag between the cursors.
struct StepResult {
    TagId tagId = 0;
    /// True when both references exist and y100 != y0. When false, `error` says why and
    /// the other fields except tagId/t1/t2 are unspecified.
    bool valid = false;
    QString error; ///< e.g. "Place both cursors", "No data at C1", "No change between C1 and C2"
    double t1 = kNaN; ///< min(C1, C2)
    double t2 = kNaN; ///< max(C1, C2)
    double y0 = kNaN;
    double y100 = kNaN;
    /// Time of the input step used for L: the ΔCO tag's 50 % crossing when a ΔCO tag gives
    /// a valid step, otherwise t1.
    double stepTime = kNaN;
    QList<SetpointResult> setpoints; ///< same order as AnalysisSettings::percents
    double deltaPv = kNaN;                   ///< y100 - y0
    std::optional<double> deltaCo;           ///< from the ΔCO tag or manualDeltaCo (nonzero)
    std::optional<double> gain;              ///< K = deltaPv / deltaCo
    std::optional<double> deadTime;          ///< L in seconds (may be negative; not clamped)
    std::optional<double> timeConstant;      ///< T in seconds (> 0 when present)
};

/// Centred moving average: out[i] = mean of the non-NaN values[j] with
/// |times[j] - times[i]| <= widthS / 2; NaN if there is none. widthS <= 0 returns `values`
/// unchanged. `times` is non-decreasing and has the same size as `values`. O(n).
QList<double> smoothValues(const QList<double> &times, const QList<double> &values, double widthS);

/// 0 % / 100 % level of `tag` at time `t` (see ReferenceMode). nullopt when the value is
/// NaN or no sample lies in the window.
std::optional<double> referenceLevel(const Recorder &recorder, TagId tag, double t,
                                     ReferenceMode mode, double windowS);

/// Time at which the series reaches `percent` of the change from y0 to y100 (y100 != y0;
/// a falling step has y100 < y0). NaN samples are skipped. Algorithm:
/// 1. dir = sign(y100 - y0); "at or past" level L means (v - L) * dir >= 0.
/// 2. anchor = first sample at or past the 50 % level; none -> nullopt.
/// 3. percent >= 50: first sample k >= anchor at or past the target; the time is linearly
///    interpolated between the previous non-NaN sample and k (k itself if it is the first
///    sample or exactly on the target). None -> nullopt.
/// 4. percent < 50: last sample k < anchor with (v - target) * dir <= 0 (still before or on
///    the target); the time is interpolated between k and the next non-NaN sample (k itself
///    if exactly on the target). None -> nullopt.
std::optional<double> findCrossingTime(const QList<double> &times, const QList<double> &values,
                                       double y0, double y100, double percent);

/// Step of the controller-output tag between t1 and t2 (any order): delta = y100 - y0 with
/// the settings' reference mode, stepTime = its 50 % crossing (no smoothing).
struct StepInput {
    double delta = 0.0;
    double stepTime = 0.0;
};
/// nullopt when a reference is missing, the change is 0, or no 50 % crossing exists.
std::optional<StepInput> analyzeStepInput(const Recorder &recorder, TagId coTag, double t1,
                                          double t2, const AnalysisSettings &settings);

/// Full analysis of `pvTag` between the cursors t1 and t2 (any order):
/// - y0/y100 from referenceLevel() at t1/t2. The searched rows start at the samples that
///   define y0 (CursorValue: the row at or before t1; WindowMean: t1 - W) and end at t2, so
///   0 % is always found (at worst at the C1 sample, possibly slightly before t1). They are
///   smoothed with settings.smoothingS, then findCrossingTime() runs for every percent.
///   analyzeStepInput() searches the same way.
/// - ΔCO: analyzeStepInput() when settings.deltaCoTagId != 0 and != pvTag (if that gives
///   nullopt there is no ΔCO; it does not fall back to manualDeltaCo); when deltaCoTagId == 0,
///   manualDeltaCo if nonzero. stepTime as documented in StepResult.
/// - Model (first-order plus dead time) from the crossings ta, tb of modelPercentA/B
///   (a, b as fractions): T = (tb - ta) / (ln(1 - a) - ln(1 - b)),
///   L = ta - T * (-ln(1 - a)) - stepTime; both nullopt when either crossing is missing,
///   a >= b, or T <= 0. K = deltaPv / deltaCo when deltaCo is present.
StepResult analyzeStep(const Recorder &recorder, TagId pvTag, double t1, double t2,
                       const AnalysisSettings &settings);

} // namespace mb
