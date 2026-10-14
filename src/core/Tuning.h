#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace mb {

/// PID tuning rules for a first-order-plus-dead-time model (gain K, dead time L, time constant T).
enum class TuningRule : quint8 {
    ZieglerNichols, ///< Ziegler-Nichols open loop (reaction curve)
    CohenCoon,
    ImcLambda,      ///< IMC / lambda tuning, closed-loop time constant lambda
    Simc,           ///< Skogestad SIMC, tau_c = lambda
};

/// "Ziegler-Nichols (open loop)", "Cohen-Coon", "IMC / lambda", "SIMC".
QString toString(TuningRule rule);

enum class ControllerKind : quint8 { P, PI, PID };

/// "P", "PI", "PID".
QString toString(ControllerKind kind);

/// One controller of a rule, in the ideal (ISA) form u = Kp (e + 1/Ti integral(e dt) + Td de/dt).
/// Kp is in CO units per PV unit (1/K's units); Ti and Td in seconds.
struct TuningRow {
    ControllerKind kind = ControllerKind::P;
    /// False when the rule gives no setting for this controller (IMC/lambda P, SIMC PID): kp, ti
    /// and td are then nullopt.
    bool available = false;
    std::optional<double> kp;
    std::optional<double> ti; ///< nullopt for P
    std::optional<double> td; ///< nullopt for P and PI
    friend bool operator==(const TuningRow &, const TuningRow &) = default;
};

/// lambda when > 0, else L (the default closed-loop time constant for IMC and SIMC).
double effectiveLambda(double L, double lambda);

/// The P, PI and PID rows (always three, in that order) of `rule` for the model (K, L, T). Empty
/// when K is 0 or not finite, or L <= 0, or T <= 0, or either is not finite. A negative K gives a
/// negative Kp (direct-acting controller). With r = L / T and lam = effectiveLambda(L, lambda):
/// - ZieglerNichols: P Kp = T / (K L); PI Kp = 0.9 T / (K L), Ti = L / 0.3;
///   PID Kp = 1.2 T / (K L), Ti = 2 L, Td = 0.5 L.
/// - CohenCoon: P Kp = (T / (K L)) (1 + r / 3); PI Kp = (T / (K L)) (0.9 + r / 12),
///   Ti = L (30 + 3 r) / (9 + 20 r); PID Kp = (T / (K L)) (4 / 3 + r / 4),
///   Ti = L (32 + 6 r) / (13 + 8 r), Td = 4 L / (11 + 2 r).
/// - ImcLambda: P not available; PI Kp = T / (K (lam + L)), Ti = T;
///   PID Kp = (T + L / 2) / (K (lam + L / 2)), Ti = T + L / 2, Td = T L / (2 T + L).
/// - Simc (tau_c = lam): P Kp = T / (K (lam + L)); PI Kp = T / (K (lam + L)),
///   Ti = min(T, 4 (lam + L)); PID not available (needs a second-order model).
QList<TuningRow> tune(double K, double L, double T, TuningRule rule, double lambda = 0.0);

} // namespace mb
