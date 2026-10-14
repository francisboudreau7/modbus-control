#include "Tuning.h"

// Stubs created by T190; implemented by T192.

namespace mb {

QString toString(TuningRule rule)
{
    switch (rule) {
    case TuningRule::ZieglerNichols:
        return QStringLiteral("Ziegler-Nichols (open loop)");
    case TuningRule::CohenCoon:
        return QStringLiteral("Cohen-Coon");
    case TuningRule::ImcLambda:
        return QStringLiteral("IMC / lambda");
    case TuningRule::Simc:
        return QStringLiteral("SIMC");
    }
    return {};
}

QString toString(ControllerKind kind)
{
    switch (kind) {
    case ControllerKind::P:
        return QStringLiteral("P");
    case ControllerKind::PI:
        return QStringLiteral("PI");
    case ControllerKind::PID:
        return QStringLiteral("PID");
    }
    return {};
}

double effectiveLambda(double L, double lambda)
{
    return lambda > 0.0 ? lambda : L;
}

QList<TuningRow> tune(double, double, double, TuningRule, double)
{
    return {};
}

} // namespace mb
