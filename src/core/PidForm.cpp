#include "PidForm.h"

#include <QCoreApplication>

#include <cmath>

namespace mb {

QString toString(PidForm form)
{
    switch (form) {
    case PidForm::Ideal:
        return QCoreApplication::translate("mb::PidForm", "Ideal (ISA)");
    case PidForm::Series:
        return QCoreApplication::translate("mb::PidForm", "Series (interacting)");
    case PidForm::Parallel:
        return QCoreApplication::translate("mb::PidForm", "Parallel");
    }
    return {};
}

QString toString(GainUnit unit)
{
    switch (unit) {
    case GainUnit::Gain:
        return QStringLiteral("Kp");
    case GainUnit::ProportionalBand:
        return QCoreApplication::translate("mb::PidForm", "PB (%)");
    }
    return {};
}

QString toString(IntegralUnit unit)
{
    switch (unit) {
    case IntegralUnit::Time:
        return QCoreApplication::translate("mb::PidForm", "Time");
    case IntegralUnit::RepeatsPerMinute:
        return QCoreApplication::translate("mb::PidForm", "Repeats/min");
    }
    return {};
}

namespace {

bool isSet(const std::optional<double> &v)
{
    return v && *v != 0.0 && !std::isnan(*v);
}

/// Time-like value present (> 0) in an ideal setting.
bool hasTime(const std::optional<double> &v)
{
    return v && *v > 0.0;
}

} // namespace

FormValues toForm(const IdealPid &pid, const ControllerSettings &settings, const PidSpans &spans,
                  bool minutes)
{
    FormValues out;
    const bool hasI = hasTime(pid.ti);
    const bool hasD = hasTime(pid.td);

    double gain = pid.kp;
    std::optional<double> ti = hasI ? pid.ti : std::nullopt;
    std::optional<double> td = hasD ? pid.td : std::nullopt;

    if (settings.form == PidForm::Series && hasI && hasD) {
        const double r = 1.0 - 4.0 * *pid.td / *pid.ti;
        if (r < 0.0) {
            out.available = false;
            out.reason = QCoreApplication::translate("mb::PidForm",
                                                     "No series equivalent: Ti is less than 4 Td.");
            return out;
        }
        const double q = std::sqrt(r);
        gain = pid.kp * (1.0 + q) / 2.0;
        ti = *pid.ti * (1.0 + q) / 2.0;
        td = *pid.ti * (1.0 - q) / 2.0;
    }

    // Gain as a percentage gain when the proportional band is asked for.
    double gainUsed = gain;
    if (settings.gain == GainUnit::ProportionalBand) {
        if (!isSet(spans.pv) || !isSet(spans.co)) {
            out.available = false;
            out.reason = QCoreApplication::translate(
                "mb::PidForm", "The proportional band needs the PV and CO ranges.");
            return out;
        }
        gainUsed = gain * *spans.pv / *spans.co;
        if (gainUsed != 0.0)
            out.gain = 100.0 / gainUsed;
    } else {
        out.gain = gain;
    }

    const double tf = minutes ? 1.0 / 60.0 : 1.0; // seconds -> shown time unit
    if (settings.form == PidForm::Parallel) {
        if (ti)
            out.integral = gainUsed / (*ti * tf);
        if (td)
            out.derivative = gainUsed * *td * tf;
    } else {
        if (ti) {
            out.integral = settings.integral == IntegralUnit::RepeatsPerMinute ? 60.0 / *ti
                                                                               : *ti * tf;
        }
        if (td)
            out.derivative = *td * tf;
    }
    return out;
}

std::optional<IdealPid> fromForm(const FormValues &values, const ControllerSettings &settings,
                                 const PidSpans &spans, bool minutes)
{
    if (!values.available || !values.gain || std::isnan(*values.gain) || *values.gain == 0.0)
        return std::nullopt;

    double gain = *values.gain;     // Kp / Kc' in the form's own domain
    double gainUsed = gain;         // the gain the Parallel Ki, Kd were computed with
    if (settings.gain == GainUnit::ProportionalBand) {
        if (!isSet(spans.pv) || !isSet(spans.co))
            return std::nullopt;
        gainUsed = 100.0 / *values.gain; // g%
        gain = gainUsed * *spans.co / *spans.pv;
    }

    const double ts = minutes ? 60.0 : 1.0; // shown time unit -> seconds
    std::optional<double> ti; // seconds
    std::optional<double> td;

    const auto &integral = values.integral;
    const auto &derivative = values.derivative;
    if ((integral && std::isnan(*integral)) || (derivative && std::isnan(*derivative)))
        return std::nullopt;

    if (settings.form == PidForm::Parallel) {
        if (isSet(integral)) {
            const double t = gainUsed / *integral * ts;
            if (!(t > 0.0))
                return std::nullopt;
            ti = t;
        }
        if (derivative) {
            const double t = *derivative / gainUsed * ts;
            if (t < 0.0)
                return std::nullopt;
            if (t > 0.0)
                td = t;
        }
    } else {
        if (integral) {
            if (settings.integral == IntegralUnit::RepeatsPerMinute) {
                if (!(*integral > 0.0))
                    return std::nullopt;
                ti = 60.0 / *integral;
            } else {
                if (*integral < 0.0)
                    return std::nullopt;
                if (*integral > 0.0)
                    ti = *integral * ts;
            }
        }
        if (derivative) {
            if (*derivative < 0.0)
                return std::nullopt;
            if (*derivative > 0.0)
                td = *derivative * ts;
        }
    }

    IdealPid pid;
    pid.kp = gain;
    pid.ti = ti;
    pid.td = td;
    if (settings.form == PidForm::Series && ti && td) {
        const double sum = *ti + *td; // Ti = Ti' + Td'
        pid.kp = gain * (1.0 + *td / *ti);
        pid.td = *ti * *td / sum;
        pid.ti = sum;
    }
    return pid;
}

} // namespace mb
