#include "ClosedLoopSim.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mb {

ClosedLoopSeries simulateClosedLoop(const SimulationSetup &setup)
{
    const ProcessModel &m = setup.model;
    const double ti = setup.pid.ti.value_or(0.0);
    const double td = setup.pid.td.value_or(0.0);
    const double dt = setup.dtS;
    const bool valid = m.t > 0.0 && m.l >= 0.0 && dt > 0.0 && setup.durationS > 0.0 && dt <= setup.durationS
                       && ti >= 0.0 && td >= 0.0 && setup.coMin < setup.coMax && std::isfinite(m.t)
                       && std::isfinite(m.l) && std::isfinite(m.k) && std::isfinite(setup.durationS)
                       && std::isfinite(dt) && std::isfinite(setup.pid.kp);
    if (!valid)
        return {};

    const int n = static_cast<int>(std::llround(setup.durationS / dt)) + 1;
    const int delaySteps = static_cast<int>(std::llround(m.l / dt));
    const double alpha = 1.0 - std::exp(-dt / m.t);
    const double kp = setup.pid.kp;
    const bool setpoint = setup.test == ClosedLoopTest::SetpointStep;
    const double spAfter = setpoint ? setup.pv0 + setup.spStep : setup.pv0;
    const double load = setpoint ? 0.0 : setup.loadStep;

    ClosedLoopSeries out;
    out.t.reserve(n);
    out.pv.reserve(n);
    out.sp.reserve(n);
    out.co.reserve(n);
    out.t.append(0.0);
    out.pv.append(setup.pv0);
    out.sp.append(setup.pv0);
    out.co.append(setup.co0);

    double dev = 0.0;          // PV - pv0
    double pvFilt = setup.pv0; // filtered PV for the derivative
    double integral = 0.0;
    for (int i = 1; i < n; ++i) {
        const double sp = spAfter;
        const double pvPrev = out.pv[i - 1];
        const double e = sp - pvPrev;
        const double p = kp * e;

        double d = 0.0;
        if (td > 0.0) {
            const double old = pvFilt;
            pvFilt += (pvPrev - pvFilt) * (1.0 - std::exp(-dt / (td / 10.0)));
            d = -kp * td * (pvFilt - old) / dt;
        } else {
            pvFilt = pvPrev;
        }

        if (ti > 0.0) {
            const double step = kp / ti * e * dt;
            const double u = setup.co0 + p + integral + step + d;
            const bool pushesHigh = u > setup.coMax && step > 0.0;
            const bool pushesLow = u < setup.coMin && step < 0.0;
            if (!pushesHigh && !pushesLow)
                integral += step;
        }
        const double u = std::clamp(setup.co0 + p + integral + d, setup.coMin, setup.coMax);
        out.co.append(u);

        const int j = i - delaySteps;
        const double uDelayed = j >= 1 ? out.co[j] : setup.co0;
        dev += (m.k * (uDelayed + load - setup.co0) - dev) * alpha;

        out.t.append(i * dt);
        out.pv.append(setup.pv0 + dev);
        out.sp.append(sp);
    }
    return out;
}

} // namespace mb
