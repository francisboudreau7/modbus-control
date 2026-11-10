#include "PidForm.h"

#include <QCoreApplication>

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

// Stubs until the controller-form task.
FormValues toForm(const IdealPid &, const ControllerSettings &, const PidSpans &, bool)
{
    return {};
}

std::optional<IdealPid> fromForm(const FormValues &, const ControllerSettings &, const PidSpans &,
                                 bool)
{
    return std::nullopt;
}

} // namespace mb
