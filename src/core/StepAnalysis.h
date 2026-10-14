#pragma once

#include "Recorder.h"
#include "Tag.h"
#include "Tuning.h"

#include <QList>
#include <QString>

#include <limits>
#include <optional>

namespace mb {

/// How the 0 % (y0) and 100 % (y100) levels of a step response are taken.
enum class ReferenceMode : quint8 {
    CursorValue, ///< recorded value at the cursor time, interpolated between the neighbouring rows
    WindowMean,  ///< mean of the non-NaN samples with time in [cursor - W, cursor]
};

/// Named two-point methods for the model's L and T. Each is a pair of percentages (a, b) used by
/// the general two-point formula of analyzeStep(); with these pairs it gives the textbook
/// constants (to 3 digits):
/// - ZeroTo632: 0 % / 63.2 %: T = t63.2 - t0, L = t0 (from the step time).
/// - Smith: 28.3 % / 63.2 %: T = 1.5 (t63.2 - t28.3), L = t63.2 - T.
/// - Sundaresan: Sundaresan-Krishnaswamy, 35.3 % / 85.3 %: T = 0.67 (t85.3 - t35.3),
///   L = 1.3 t35.3 - 0.29 t85.3.
/// - Custom: AnalysisSettings::modelPercentA / modelPercentB.
enum class ModelMethod : quint8 { ZeroTo632, Smith, Sundaresan, Custom };

/// "0 % / 63.2 % (two points)", "Smith (28.3 % / 63.2 %)", "Sundaresan-Krishnaswamy (35.3 % /
/// 85.3 %)", "Custom".
QString toString(ModelMethod method);
/// (a, b) in percent for a named method; nullopt for Custom.
std::optional<std::pair<double, double>> methodPercents(ModelMethod method);

/// User settings of the Analysis panel (saved in the project, see ProjectIO.h).
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
    /// Method for the model's two points; Custom uses modelPercentA/B.
    ModelMethod method = ModelMethod::ZeroTo632;
    /// Setpoints a < b used for the model when method == Custom, both in [0, 100).
    double modelPercentA = 0.0;
    double modelPercentB = 63.2;
    /// Process-value tag analysed for the model and tuning. 0 = none chosen.
    TagId pvTagId = 0;
    /// Tags that show setpoint markers, guide lines and setpoint-table rows (the analysis runs for
    /// each). The UI checks the PV when it is chosen. May contain ids that no longer exist.
    QList<TagId> setpointTagIds;
    /// Controller-output tag whose change between the cursors is ΔCO. 0 = use manualDeltaCo.
    /// An id that is not in the project or not recorded is treated like "no ΔCO".
    TagId deltaCoTagId = 0;
    /// ΔCO typed by the user (in the CO's units) when deltaCoTagId == 0. 0 = unknown (no K).
    double manualDeltaCo = 0.0;
    /// Plot labels of the setpoint markers: false = "63.2 %"; true = three lines
    /// "63.2 %", "t = 14.360 s", "y = 22.64 degC" (target value and tag unit).
    bool detailedLabels = false;
    /// Draw the fitted model curve of the PV (see fopdtModelValue) on the plot.
    bool showModel = true;
    /// Dashed guide lines from each found setpoint marker down to the time axis and left to
    /// the Y axis.
    bool guideLines = true;
    /// Optional ranges (spans) to also show gains in %/%: K% = K * coSpan / pvSpan and
    /// Kp% = Kp * pvSpan / coSpan, with span = max - min. The PV range is set only when both
    /// values are; a span of 0 counts as not set.
    std::optional<double> pvRangeMin;
    std::optional<double> pvRangeMax;
    double coRangeMin = 0.0;
    double coRangeMax = 100.0;
    /// Tuning calculator: the rule shown, lambda in seconds (0 = auto = L, see effectiveLambda)
    /// and whether Ti / Td are shown in minutes (else seconds).
    TuningRule tuningRule = TuningRule::ZieglerNichols;
    double lambdaS = 0.0;
    bool timeInMinutes = false;

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
    /// Root-mean-square of (recorded − fopdtModelValue) over the non-NaN rows with time in
    /// [t1, t2] (raw values, not smoothed). Present when deadTime and timeConstant are.
    std::optional<double> fitRms;
    /// fitRms / |deltaPv| * 100.
    std::optional<double> fitRmsPercent;
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
/// - y0/y100 from referenceLevel() at t1/t2. The rows of [t1, t2] are smoothed with
///   settings.smoothingS; findCrossingTime() then runs for every percent on the series
///   (t1, y0), the smoothed rows, (t2, y100). So every time found lies in [t1, t2]: 0 % is
///   at or after C1 (C1 itself when the signal never comes back to y0 after C1) and 100 % at
///   or before C2. analyzeStepInput() searches the same way (without smoothing).
///   `measured` interpolates the recorded rows, with the interpolated values at t1 and t2.
/// - ΔCO: analyzeStepInput() when settings.deltaCoTagId != 0 and != pvTag (if that gives
///   nullopt there is no ΔCO; it does not fall back to manualDeltaCo); when deltaCoTagId == 0,
///   manualDeltaCo if nonzero. stepTime as documented in StepResult.
/// - Model (first-order plus dead time) from the crossings ta, tb of the method's percents
///   (methodPercents(settings.method), or modelPercentA/B for Custom; a, b as fractions). The
///   crossings are searched even when those percents are not in settings.percents: T = (tb - ta) / (ln(1 - a) - ln(1 - b)),
///   L = ta - T * (-ln(1 - a)) - stepTime; both nullopt when either crossing is missing,
///   a >= b, or T <= 0. K = deltaPv / deltaCo when deltaCo is present. fitRms and
///   fitRmsPercent as documented in StepResult.
/// Model response at time t: y0 for t <= stepTime + L, else
/// y0 + deltaPv * (1 - exp(-(t - stepTime - L) / T)). NaN when the result is invalid or has
/// no deadTime/timeConstant. Uses neither K nor ΔCO.
double fopdtModelValue(const StepResult &result, double t);

StepResult analyzeStep(const Recorder &recorder, TagId pvTag, double t1, double t2,
                       const AnalysisSettings &settings);

/// (a, b) the model uses: methodPercents(settings.method), or (modelPercentA, modelPercentB) for
/// Custom.
std::pair<double, double> modelPercents(const AnalysisSettings &settings);

/// Cursor positions around a controller-output step, found by findStep().
struct StepWindow {
    double c1 = kNaN;       ///< just before the step
    double c2 = kNaN;       ///< after the PV has settled (or the end of the range)
    double stepTime = kNaN; ///< time of the CO jump (the later sample of the largest jump)
    bool settled = false;   ///< false when the PV had not settled before the end of the range
};

/// Finds the controller-output step to analyse in [from, to] (from < to; the caller passes the
/// visible time range, or the whole recording). Algorithm (non-NaN samples only):
/// 1. jump = the largest |co[i] - co[i-1]| between consecutive samples with both times in
///    [from, to] (ties: the latest). None, or 0 -> nullopt. stepTime = time of sample i.
/// 2. prev = time of the latest earlier CO change (any nonzero difference between consecutive
///    samples before i) or `from`; c1 = stepTime - min(5 s, (stepTime - prev) / 2).
/// 3. end = time of the first CO change after i (sample time before the change), or `to`.
/// 4. On the PV in [stepTime, end]: y0 = PV at c1 (Recorder value interpolation); final = mean of
///    the PV samples in the last 10 % of [stepTime, end]; settled at the first sample time ts
///    after which every sample until end is within 2 % of |final - y0| of final (when
///    final == y0: never settled). settled -> c2 = min(end, ts + 0.2 (ts - stepTime)); otherwise
///    c2 = end and settled = false.
/// nullopt also when the CO or PV tag has no samples in the range.
std::optional<StepWindow> findStep(const Recorder &recorder, TagId coTag, TagId pvTag, double from,
                                   double to);

/// Plain-language checks of a PV result (see AnalysisPanel for the wording).
enum class StepCheck : quint8 {
    NoDeltaCo,        ///< result valid but no deltaCo: K (and the tuning) can't be computed
    NoModel,          ///< result valid but no deadTime/timeConstant (a crossing was not found)
    NotSettled,       ///< the PV still moves at C2: least-squares slope s of the raw PV samples in
                      ///< [t2 - 0.1 (t2 - t1), t2] (at least 3 samples) with |s| * 0.1 (t2 - t1)
                      ///< > 0.05 |deltaPv|
    SmallStep,        ///< |deltaPv| < 5 * standard deviation of the raw PV samples in
                      ///< [t1 - 0.1 (t2 - t1), t1] (at least 3 samples)
    NegativeDeadTime, ///< deadTime < 0 (C1 after the response started, or wrong step time)
    DeadTimeDominant, ///< deadTime / timeConstant > 1
    FitPoor,          ///< fitQuality() == Poor
};

/// Good: fitRmsPercent < 3; Acceptable: < 8; Poor: otherwise.
enum class FitQuality : quint8 { Good, Acceptable, Poor };
FitQuality fitQuality(double fitRmsPercent);

/// Checks that apply to `result` (the PV's analyzeStep result), in enum order. An invalid result
/// gives an empty list (its error text is shown instead).
QList<StepCheck> checkStep(const Recorder &recorder, const StepResult &result);

} // namespace mb
