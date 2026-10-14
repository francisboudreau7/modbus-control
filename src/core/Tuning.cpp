#include "Tuning.h"

#include <algorithm>
#include <cmath>

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

namespace {

TuningRow makeRow(ControllerKind kind, double kp, std::optional<double> ti = std::nullopt,
                  std::optional<double> td = std::nullopt)
{
    TuningRow row;
    row.kind = kind;
    row.available = true;
    row.kp = kp;
    row.ti = ti;
    row.td = td;
    return row;
}

TuningRow unavailableRow(ControllerKind kind)
{
    TuningRow row;
    row.kind = kind;
    row.available = false;
    return row;
}

} // namespace

QList<TuningRow> tune(double K, double L, double T, TuningRule rule, double lambda)
{
    if (K == 0.0 || !std::isfinite(K) || L <= 0.0 || !std::isfinite(L) || T <= 0.0
        || !std::isfinite(T)) {
        return {};
    }

    const double lam = effectiveLambda(L, lambda);
    const double r = L / T;
    QList<TuningRow> rows;

    switch (rule) {
    case TuningRule::ZieglerNichols:
        rows.append(makeRow(ControllerKind::P, T / (K * L)));
        rows.append(makeRow(ControllerKind::PI, 0.9 * T / (K * L), L / 0.3));
        rows.append(makeRow(ControllerKind::PID, 1.2 * T / (K * L), 2.0 * L, 0.5 * L));
        break;

    case TuningRule::CohenCoon:
        rows.append(makeRow(ControllerKind::P, (T / (K * L)) * (1.0 + r / 3.0)));
        rows.append(makeRow(ControllerKind::PI, (T / (K * L)) * (0.9 + r / 12.0),
                            L * (30.0 + 3.0 * r) / (9.0 + 20.0 * r)));
        rows.append(makeRow(ControllerKind::PID, (T / (K * L)) * (4.0 / 3.0 + r / 4.0),
                            L * (32.0 + 6.0 * r) / (13.0 + 8.0 * r),
                            4.0 * L / (11.0 + 2.0 * r)));
        break;

    case TuningRule::ImcLambda:
        rows.append(unavailableRow(ControllerKind::P));
        rows.append(makeRow(ControllerKind::PI, T / (K * (lam + L)), T));
        rows.append(makeRow(ControllerKind::PID, (T + L / 2.0) / (K * (lam + L / 2.0)),
                            T + L / 2.0, T * L / (2.0 * T + L)));
        break;

    case TuningRule::Simc:
        rows.append(makeRow(ControllerKind::P, T / (K * (lam + L))));
        rows.append(makeRow(ControllerKind::PI, T / (K * (lam + L)),
                            std::min(T, 4.0 * (lam + L))));
        rows.append(unavailableRow(ControllerKind::PID));
        break;
    }

    return rows;
}

} // namespace mb
