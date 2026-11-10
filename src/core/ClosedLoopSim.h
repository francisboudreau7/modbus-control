#pragma once

#include "ClosedLoopAnalysis.h"
#include "PidForm.h"

#include <QList>

namespace mb {

/// v1.4.0: predicted closed-loop response of a first-order-plus-dead-time model under a PID, for
/// the "Predicted" column and dashed curves of the closed-loop area. Deterministic, no noise.

/// FOPDT model in deviation variables: T dy/dt = -y + K u(t - L). K in PV units per CO unit,
/// L >= 0 and T > 0 in s. (1.4.1 adds integrating models.)
struct ProcessModel {
    double k = 1.0;
    double l = 0.0;
    double t = 1.0;
};

struct SimulationSetup {
    ProcessModel model;
    IdealPid pid; ///< u = Kp (e + 1/Ti integral e dt - Td dPVf/dt), e = SP - PV (Kp sign = action)
    ClosedLoopTest test = ClosedLoopTest::SetpointStep;
    double pv0 = 0.0;      ///< PV at start (steady state)
    double co0 = 0.0;      ///< CO at start (steady state for pv0)
    double spStep = 0.0;   ///< SetpointStep: SP goes from pv0 to pv0 + spStep at time 0
    /// Disturbance: a load step of this size in CO units at the process input at time 0 (the
    /// process sees u + load), SP stays pv0.
    double loadStep = 0.0;
    double coMin = 0.0;    ///< output limits
    double coMax = 100.0;
    double durationS = 60.0;
    double dtS = 0.1;      ///< integration and controller period (0 < dt <= duration)
};

/// Simulates from time 0 to durationS (inclusive) with step dtS (n = round(duration / dt) + 1
/// samples, t[i] = i dt). Each step: the controller runs on the PV of the previous step:
/// P = Kp e; I += Kp / Ti e dt only when the new output stays within [coMin, coMax] or e pushes it
/// back inside (conditional integration, as in the Loop Simulator); D = -Kp Td dPVf/dt on a
/// first-order filter of the PV with time constant Td / 10 (no D kick on SP steps); u = clamp(co0 +
/// P + I + D, coMin, coMax) with I starting at 0. The process: the input u_d is u delayed by
/// round(L / dt) steps (a buffer holding co0 at the start); with dev = PV - pv0, each step
/// dev += (K (u_d + load - co0) - dev) (1 - exp(-dt / T)) (exact discretisation of the lag).
/// Returns the series with t, pv, co and sp (sp[0] = pv0; sp[i > 0] = pv0 + spStep for
/// SetpointStep, pv0 for Disturbance). Invalid inputs (T <= 0, dt <= 0, L < 0, duration <= 0,
/// Ti < 0, Td < 0, coMin >= coMax) return an empty series.
ClosedLoopSeries simulateClosedLoop(const SimulationSetup &setup);

} // namespace mb
