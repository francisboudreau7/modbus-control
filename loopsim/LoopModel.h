#pragma once

#include <QtGlobal>

#include <cstdint>
#include <deque>
#include <random>

namespace mbloop {

/// Fixed physical constants of level loop 101 (see docs/LOOP_SIMULATOR.md). SI units.
namespace loop101 {
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTankDiameterM = 0.30;
inline constexpr double kTankAreaM2 = kPi * 0.15 * 0.15; ///< 0.070686 m^2
inline constexpr double kTankHeightM = 1.0;              ///< LT-101 range 0..1 m = 0..100 %
inline constexpr double kOverflowLevelM = 0.99;          ///< overflow pipe at 99 %
inline constexpr double kPumpMaxPressureBar = 1.0;       ///< P-101 at 100 % speed
inline constexpr double kFcvMaxFlowM3s = 5.0e-3;         ///< 300 L/min: FCV fully open, pump 100 %
inline constexpr double kHvCoefficient = 7.0711e-3;      ///< Chv in m^2.5/s (Qhv = Chv * v * sqrt(h))
inline constexpr double kStepS = 0.010;                  ///< integration step, simulation time
inline constexpr double kNoisePeriodS = 0.100;           ///< LT noise sample period
inline constexpr double kPidPeriodS = 0.100;             ///< LIC-101 PID execution period
} // namespace loop101

/// Settings a teacher can change (window sliders, scenario file, command line).
struct LoopParams {
    double pumpSpeedPct = 100.0;  ///< P-101 speed, 0..100 %
    double hvOpeningPct = 50.0;   ///< HV-102 opening, 0..100 %
    double deadTimeS = 2.0;       ///< pipe transport delay FCV-101 -> TK-101, 0..30 s
    double ltNoisePct = 0.05;      ///< LT-101 noise amplitude n: uniform in [-n, +n] %, 0..5
    double ltDampingS = 2.0;      ///< LT-101 first-order damping time constant, 0..60 s (0 = none)
    double fcvStrokeS = 5.0;      ///< FCV-101 full-stroke time, 0..120 s (0 = instant)
    friend bool operator==(const LoopParams &, const LoopParams &) = default;
};

/// LIC-101 PID settings (ideal ISA form u = Kp (e + 1/Ti integral(e dt) + Td de/dt)).
struct PidSettings {
    double kp = 1.0;      ///< proportional gain, 0..100 (% OUT per % error)
    double tiS = 30.0;    ///< integral time, 0..3600 s (0 = no integral action)
    double tdS = 0.0;     ///< derivative time, 0..600 s (0 = no derivative action)
    bool reverse = true;  ///< Reverse: e = SP - PV (PV up -> OUT down); Direct: e = PV - SP
    friend bool operator==(const PidSettings &, const PidSettings &) = default;
};

/// Instantaneous values (for the window, the server and tests). Flows in m^3/s.
struct LoopState {
    double timeS = 0.0;          ///< simulation time since reset
    double levelM = 0.0;         ///< true level h in TK-101, 0..kOverflowLevelM
    double outPct = 0.0;         ///< LIC-101 OUT (FCV command), 0..100 %
    double spPct = 50.0;         ///< LIC-101 SP, 0..100 % (stored only)
    double stemPct = 0.0;        ///< FCV-101 stem position, 0..100 %
    double pvPct = 0.0;          ///< LT-101 output = LIC-101 PV, 0..100 %
    double pumpPressureBar = 0.0;///< P-101 discharge pressure
    double qFcv = 0.0;           ///< flow through FCV-101 now
    double qIn = 0.0;            ///< flow arriving in TK-101 (qFcv delayed by the dead time)
    double qHv = 0.0;            ///< flow out through HV-102
    double qOverflow = 0.0;      ///< flow out through the overflow (> 0 only at kOverflowLevelM)
    bool overflowing = false;    ///< qOverflow > 0
    bool autoMode = false;       ///< LIC-101 in Auto (OUT computed by the PID)
};

/// 4..20 mA for 0..100 % (not clamped).
inline double percentToMa(double pct) { return 4.0 + 16.0 * pct / 100.0; }

/// Level loop 101 process model: TK-102 (infinite) -> P-101 -> FCV-101 -> pipe (dead time) ->
/// TK-101 -> HV-102 / overflow -> TK-102. Pure computation, deterministic for a given seed.
///
/// Equations (s = pump speed / 100, y = stem / 100, v = HV opening / 100, h = level in m):
/// - pump pressure p = kPumpMaxPressureBar * s^2;
/// - FCV flow qFcv = kFcvMaxFlowM3s * y * sqrt(p / kPumpMaxPressureBar) = kFcvMaxFlowM3s * y * s;
/// - the stem moves toward OUT at most 100 / fcvStrokeS % per second (fcvStrokeS 0: stem = OUT);
/// - qIn(t) = qFcv(t - deadTimeS) (ring buffer of qFcv per integration step; deadTimeS 0: qIn =
///   qFcv); changing the dead time keeps the buffer's history (shorter: drop the oldest; longer:
///   pad with the oldest value);
/// - qHv = kHvCoefficient * v * sqrt(max(h, 0));
/// - dh/dt = (qIn - qHv) / kTankAreaM2, explicit Euler with kStepS; then if h > kOverflowLevelM:
///   qOverflow = (h - kOverflowLevelM) * kTankAreaM2 / kStepS and h = kOverflowLevelM; h < 0 ->
///   h = 0;
/// - LT-101: the sensor reads 100 * h / kTankHeightM + noise, where a new noise value uniform in
///   [-ltNoisePct, +ltNoisePct] is drawn every kNoisePeriodS of simulation time (std::mt19937
///   seeded by the constructor's seed); the transmitter damping, a first-order lag ltDampingS,
///   filters that reading (exact discretisation L += (target - L) * (1 - exp(-dt / tau)); tau 0:
///   L = target), so the damping also smooths the noise (v1.3.2; before, the noise was added after
///   the damping); pvPct = clamp(L, 0, 100).
/// Steady state (no overflow): h = (kFcvMaxFlowM3s * y * s / (kHvCoefficient * v))^2; with pump
/// 100 %, HV 50 %: OUT 50 % -> 0.50 m, 30 % -> 0.18 m, 70 % -> 0.98 m. Linearised time constant
/// tau = 2 * kTankAreaM2 * h / qHv (about 28 s at 0.50 m, 150 L/min). The flows are sized so a step
/// test settles in about 2.5 minutes of real time (v1.3.0: 5 times the first design's flows; the
/// settled levels and gains are unchanged).
class LoopModel
{
public:
    explicit LoopModel(quint32 seed = 1);

    /// Resets to the initial state for `params` with OUT = SP = `outPct`: stem = OUT, level at
    /// the steady state for that OUT (clamped to kOverflowLevelM), the dead-time buffer filled
    /// with the steady qFcv, LT lag at the level, noise cleared to 0, time 0.
    void reset(const LoopParams &params, double outPct = 50.0);

    /// Advances the simulation by `seconds` of simulation time (>= 0) in kStepS steps (the
    /// remainder below one step is carried to the next call, so many small calls equal one
    /// big call).
    void advance(double seconds);

    const LoopParams &params() const;
    /// Changes the settings (clamped to their ranges) without touching the state.
    void setParams(const LoopParams &params);
    /// OUT and SP (clamped 0..100); used by the faceplate and the Modbus server.
    void setOut(double pct);
    void setSp(double pct);
    /// Sets the true level (clamped 0..kOverflowLevelM) and the LT lag to it (scenario load).
    void setLevelM(double h);
    /// Sets the stem position (clamped 0..100) (scenario load).
    void setStemPct(double pct);
    /// Refills the whole dead-time buffer with the current qFcv (scenario load, after setStemPct).
    void fillPipe();

    /// PID (v1.3.1). Executed every kPidPeriodS of simulation time (every 10 steps) while in Auto:
    /// e = SP - PV (reverse) or PV - SP (direct) using pvPct (the transmitter output, noise
    /// included); P = Kp e; I += Kp / Ti e dt (Ti 0: I is a constant bias set at the
    /// bumpless transfer (manual reset), no integration) with conditional
    /// integration (anti-windup): I is not integrated when OUT is at 0 or 100 and e would push it
    /// further; D = -Kp Td dPVf/dt for reverse (+ for direct) on a filtered PV (first-order filter
    /// with time constant Td / 10; Td 0: D = 0) - derivative on PV, so SP changes do not kick;
    /// OUT = clamp(P + I + D, 0, 100). Bumpless: entering Auto sets I = OUT - P - D for the
    /// current values (so OUT does not jump) and the PV filter to PV. Leaving Auto keeps OUT.
    /// setOut() is ignored in Auto (the server and the faceplate refuse it before).
    void setPid(const PidSettings &pid); ///< clamped to the ranges; keeps I bumpless (I adjusted so
                                         ///< OUT does not jump when Kp changes in Auto)
    const PidSettings &pid() const;
    void setAuto(bool on);
    bool isAuto() const;

    const LoopState &state() const;

    /// Steady level in m for these settings and OUT (not clamped to the overflow), for tests and
    /// the reset; +inf when the HV is closed and OUT > 0 and pump > 0; 0 when no inflow.
    static double steadyLevelM(const LoopParams &params, double outPct);

private:
    // Implementation-defined; T211 may change the private section freely.
    LoopParams m_params;
    LoopState m_state;
    std::deque<double> m_pipe;
    double m_carryS = 0.0;
    double m_lagPct = 0.0;
    double m_noisePct = 0.0;
    int m_noiseSteps = 0; ///< steps since the last noise sample
    std::mt19937 m_rng;
    PidSettings m_pid;
    double m_integral = 0.0; ///< PID I term, % OUT
    double m_pvFilt = 0.0;   ///< filtered PV for the derivative
    double m_dPv = 0.0;      ///< d(filtered PV)/dt of the last PID execution, %/s
    int m_pidSteps = 0;      ///< steps since the last PID execution

    void step();
    void resizePipe(std::size_t n, double padValue);
    void refreshPv();
    void runPid();
    double pidError() const;
    double pidD() const;
};

} // namespace mbloop
