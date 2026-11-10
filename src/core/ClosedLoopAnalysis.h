#pragma once

#include "Recorder.h"
#include "Tag.h"

#include <QList>
#include <QString>

#include <limits>
#include <optional>

namespace mb {

/// v1.4.0: closed-loop evaluation of a recorded response (the "Closed loop" area of the Analysis
/// panel). Works on any loop: it only needs the PV, the setpoint, and optionally the output.

enum class ClosedLoopTest : quint8 {
    SetpointStep, ///< the SP changes at the step; how the PV follows it
    Disturbance,  ///< the SP stays constant; how the PV comes back after a load change
};

/// Settings of the closed-loop area (saved in the project with AnalysisSettings).
struct ClosedLoopSettings {
    ClosedLoopTest test = ClosedLoopTest::SetpointStep;
    /// Used when no SP tag is chosen (AnalysisSettings::spTagId == 0): the setpoint before and after
    /// the step (SetpointStep; the step is then at C1), or the constant setpoint (Disturbance uses
    /// typedSpAfter).
    double typedSpBefore = 0.0;
    double typedSpAfter = 0.0;
    /// Settling band in % (2 or 5 in the UI; (0, 50] accepted): of |SP step| for SetpointStep,
    /// of |max deviation| for Disturbance.
    double settlingBandPct = 2.0;
    /// Predicted response (ClosedLoopSim.h) drawn on the plot and its numbers shown beside the
    /// measured ones. The settings are in the ideal form (see PidForm.h for display).
    bool showPrediction = false;
    double predictionKp = 1.0;
    double predictionTiS = 0.0; ///< 0 = no integral action
    double predictionTdS = 0.0; ///< 0 = no derivative action
    /// The process model the prediction uses (saved with the project, so it survives moving the
    /// cursors from the open-loop step to the closed-loop test). modelSet false = no model yet.
    /// The panel fills it from the open-loop result ("Use the open-loop model", and automatically
    /// while modelSet is false and the open-loop PV result has K, L >= 0 and T > 0); the student
    /// can also type it (a model from another tool or a lab handout). K in PV units per CO unit,
    /// L >= 0 and T > 0 in s. modelSource is a short display text, e.g. "Smith (28.3 % / 63.2 %),
    /// C1 = 60.0 s" or "typed"; it is not interpreted.
    bool modelSet = false;
    double modelK = 1.0;
    double modelLS = 0.0;
    double modelTS = 1.0;
    QString modelSource;
    /// Disturbance prediction: size of the load change at the process input, in CO units.
    double loadStep = 5.0;
    friend bool operator==(const ClosedLoopSettings &, const ClosedLoopSettings &) = default;
};

/// One time series sampled at increasing times (NaN values allowed and skipped).
struct ClosedLoopSeries {
    QList<double> t;
    QList<double> pv;
    QList<double> sp; ///< same size as t (the SP tag's values, or the typed setpoint)
    QList<double> co; ///< empty when no output tag is chosen, else same size as t
};

/// Plain-language checks of a closed-loop result (wording in the panel).
enum class ClosedLoopCheck : quint8 {
    NoSetpointStep,      ///< SetpointStep but |SP after - SP before| is 0 (or below 1e-12)
    SetpointMoved,       ///< Disturbance but the SP tag changes between the cursors
    NotSettled,          ///< no settling time (PV outside the band at C2)
    OutputSaturated,     ///< the output touched its range limits (numbers show the limits)
    NoOutput,            ///< no output tag: the output rows are empty
};

struct ClosedLoopResult {
    bool valid = false;
    /// Why the result is invalid, e.g. "Place both cursors", "No data between the cursors",
    /// "The setpoint does not change between the cursors".
    QString error;
    ClosedLoopTest test = ClosedLoopTest::SetpointStep;
    double t1 = std::numeric_limits<double>::quiet_NaN(); ///< min(C1, C2)
    double t2 = std::numeric_limits<double>::quiet_NaN(); ///< max(C1, C2)
    /// SetpointStep: time of the SP change (the SP tag's 50 % crossing, or t1 when typed).
    /// Disturbance: t1.
    double startTime = std::numeric_limits<double>::quiet_NaN();
    double spBefore = std::numeric_limits<double>::quiet_NaN(); ///< SetpointStep only
    double spAfter = std::numeric_limits<double>::quiet_NaN();  ///< final (or constant) SP
    double pvAtStart = std::numeric_limits<double>::quiet_NaN();

    // Times are measured from startTime, in s. nullopt = not applicable or not found.
    std::optional<double> overshoot;        ///< PV units, >= 0 (SetpointStep)
    std::optional<double> overshootPct;     ///< % of |step| (SetpointStep)
    std::optional<double> peakTime;         ///< time of the overshoot peak (overshoot > 0)
    std::optional<double> riseTime;         ///< 10 % to 90 % of the step (SetpointStep)
    std::optional<double> maxDeviation;     ///< signed SP - PV of largest |SP - PV| (Disturbance)
    std::optional<double> maxDeviationTime; ///< its time (Disturbance)
    std::optional<double> settlingTime;     ///< see analyzeClosedLoopSeries()
    std::optional<double> decayRatio;       ///< second peak / first peak, same sign
    std::optional<double> period;           ///< time between those two peaks
    double steadyStateError = std::numeric_limits<double>::quiet_NaN(); ///< SP - mean PV, last 10 %
    double iae = std::numeric_limits<double>::quiet_NaN();  ///< integral of |e| dt
    double ise = std::numeric_limits<double>::quiet_NaN();  ///< integral of e^2 dt
    double itae = std::numeric_limits<double>::quiet_NaN(); ///< integral of (t - startTime) |e| dt
    std::optional<double> outMin;           ///< output range used between startTime and t2
    std::optional<double> outMax;
    std::optional<double> outTravel;        ///< sum of |CO[i] - CO[i-1]| (total valve travel)
    QList<ClosedLoopCheck> checks;          ///< in enum order
};

/// Analysis of a series between t1 and t2 (any order; e = SP - PV everywhere). Samples with a NaN
/// PV are skipped; integrals use the trapezoid rule on the remaining samples from startTime to t2.
/// - SetpointStep: spBefore = SP at t1, spAfter = SP at t2 (series values interpolated);
///   d = spAfter - spBefore; |d| < 1e-12 -> invalid, error "The setpoint does not change between
///   the cursors" (and check NoSetpointStep). startTime = time of the first sample where SP is at
///   or past spBefore + d / 2 (a typed SP has its step at t1: the caller fills the series so).
///   overshoot = max over [startTime, t2] of (PV - spAfter) sign(d), clamped >= 0;
///   overshootPct = overshoot / |d| 100; peakTime when overshoot > 0. riseTime = t90 - t10 with the
///   first (interpolated) crossings of spBefore + 0.1 d and + 0.9 d after startTime.
/// - Disturbance: spAfter = SP at t1; maxDeviation = the e of largest |e| in [t1, t2] and its time.
///   SetpointMoved when SP varies by more than 1e-9 relative to |spAfter| (or 1e-9 absolute).
/// - Settling band b = settlingBandPct / 100 * |d| (SetpointStep) or * |maxDeviation|
///   (Disturbance). settlingTime = time of the first sample after the last sample with |e| > b,
///   minus startTime; 0 when no sample after startTime is outside the band; nullopt (check
///   NotSettled) when the last sample is outside the band.
/// - Peaks of the error with the sign of the first excursion (SetpointStep: e < 0 is overshoot,
///   so peaks of -e sign(d); Disturbance: peaks with the sign of maxDeviation): local extrema
///   (strict on one side) above b. decayRatio = second / first, period = their time difference;
///   nullopt with fewer than two.
/// - steadyStateError = spAfter - mean PV over the last 10 % of [startTime, t2] (at least one
///   sample).
/// - Output (co not empty): outMin/outMax over [startTime, t2], outTravel = sum |delta CO| over
///   consecutive non-NaN samples in [startTime, t2]; OutputSaturated when a sample is within
///   0.1 % of the range span of coRangeMin or coRangeMax. co empty: check NoOutput.
ClosedLoopResult analyzeClosedLoopSeries(const ClosedLoopSeries &series, double t1, double t2,
                                         const ClosedLoopSettings &settings, double coRangeMin,
                                         double coRangeMax);

/// Builds the series from the Recorder rows with time in [min(t1, t2), max(t1, t2)] and runs
/// analyzeClosedLoopSeries(). spTag 0 = typed setpoint: SetpointStep puts typedSpBefore on the
/// first row (at or after t1) and typedSpAfter on every later row, so the step is at the first
/// row; Disturbance puts typedSpAfter on every row. coTag 0 = no output (co empty). Fewer than
/// two rows with a PV value: invalid, error "No data between the cursors". NaN cursors: invalid,
/// error "Place both cursors".
ClosedLoopResult analyzeClosedLoop(const Recorder &recorder, TagId pvTag, TagId spTag, TagId coTag,
                                   double t1, double t2, const ClosedLoopSettings &settings,
                                   double coRangeMin, double coRangeMax);

} // namespace mb
