#pragma once

#include <QString>

#include <optional>

namespace mb {

/// v1.4.0: how a controller writes its PID settings. The app computes and simulates in the ideal
/// (ISA) form u = Kp (e + 1/Ti integral(e dt) + Td de/dt), with Kp in CO units per PV unit and
/// Ti, Td in seconds; these settings only change how the values are shown and typed.
enum class PidForm : quint8 {
    Ideal,    ///< ISA / standard / non-interacting: Kp, Ti, Td
    Series,   ///< interacting (classic analog): Kc', Ti', Td'
    Parallel, ///< independent gains: Kp, Ki = Kp / Ti, Kd = Kp Td
};
/// Gain as a gain (Kp, in CO units per PV unit) or as a proportional band in % (needs both ranges).
enum class GainUnit : quint8 { Gain, ProportionalBand };
/// Integral action of Ideal and Series as a time (s or min, see ControllerSettings::minutes) or as
/// repeats per minute (60 / Ti with Ti in s). Parallel always shows Ki.
enum class IntegralUnit : quint8 { Time, RepeatsPerMinute };

struct ControllerSettings {
    PidForm form = PidForm::Ideal;
    GainUnit gain = GainUnit::Gain;
    IntegralUnit integral = IntegralUnit::Time;
    friend bool operator==(const ControllerSettings &, const ControllerSettings &) = default;
};

QString toString(PidForm form);         ///< "Ideal (ISA)", "Series (interacting)", "Parallel" (translated)
QString toString(GainUnit unit);        ///< "Kp", "PB (%)" (translated)
QString toString(IntegralUnit unit);    ///< "Time", "Repeats/min" (translated)

/// PID settings in the ideal form. ti nullopt or 0 = no integral action; td nullopt or 0 = no
/// derivative action.
struct IdealPid {
    double kp = 0.0;
    std::optional<double> ti; ///< s
    std::optional<double> td; ///< s
    friend bool operator==(const IdealPid &, const IdealPid &) = default;
};

/// Signal spans for the percent conversions (max - min of each range); nullopt or 0 = not set.
struct PidSpans {
    std::optional<double> pv;
    std::optional<double> co;
};

/// The three values as a controller of the chosen form shows them. Each value is nullopt when
/// the controller has no such action (P only, no D) or the value can't be computed (reason).
struct FormValues {
    /// False when the form has no equivalent (Series with Ti < 4 Td) or the gain unit needs spans
    /// that are not set; `reason` then says why and the values are nullopt.
    bool available = true;
    QString reason;
    std::optional<double> gain;       ///< Kp / Kc' (gain units, see below) or PB in %
    std::optional<double> integral;   ///< Ti / Ti' (s or min), repeats per min, or Ki
    std::optional<double> derivative; ///< Td / Td' (s or min), or Kd
};

/// Converts ideal settings to `settings.form` / units. Rules (r = 1 - 4 Td / Ti):
/// - Ideal: gain Kp, integral Ti, derivative Td.
/// - Series, D present and Ti > 0: needs r >= 0, else available = false, reason "No series
///   equivalent: Ti is less than 4 Td." Kc' = Kp (1 + sqrt(r)) / 2, Ti' = Ti (1 + sqrt(r)) / 2,
///   Td' = Ti (1 - sqrt(r)) / 2. Without D (or without I) Series equals Ideal.
/// - Parallel: gain Kp, integral Ki = Kp / Ti (per s, or per min when `minutes`), derivative
///   Kd = Kp Td (s, or min when `minutes`).
/// - Gain unit ProportionalBand: the form's gain g (Kp or Kc') becomes g% = g pv / co (both spans
///   needed, else available = false, reason "The proportional band needs the PV and CO ranges.")
///   and the shown gain is PB = 100 / g% (keeps the sign: a negative PB means direct action).
///   For Parallel, Ki and Kd are then also given with g% instead of g.
/// - Integral unit RepeatsPerMinute (Ideal and Series only): 60 / Ti (Ti in s). Time: Ti in s,
///   or in min when `minutes`. Td / Td' in s, or min when `minutes`.
FormValues toForm(const IdealPid &pid, const ControllerSettings &settings, const PidSpans &spans,
                  bool minutes);

/// Inverse of toForm(): values typed in the chosen form back to the ideal form (same units and
/// rules). nullopt when they can't be converted (PB without spans, PB or gain 0, a negative time,
/// repeats/min 0). Round trip: fromForm(toForm(p)) == p within 1e-9 relative when available.
std::optional<IdealPid> fromForm(const FormValues &values, const ControllerSettings &settings,
                                 const PidSpans &spans, bool minutes);

} // namespace mb
