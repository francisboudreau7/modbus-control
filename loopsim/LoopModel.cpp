#include "LoopModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mbloop {

namespace {

using namespace loop101;

double clampd(double v, double lo, double hi)
{
    if (std::isnan(v))
        return lo;
    return std::clamp(v, lo, hi);
}

LoopParams clampedParams(const LoopParams &p)
{
    LoopParams c;
    c.pumpSpeedPct = clampd(p.pumpSpeedPct, 0.0, 100.0);
    c.hvOpeningPct = clampd(p.hvOpeningPct, 0.0, 100.0);
    c.deadTimeS = clampd(p.deadTimeS, 0.0, 30.0);
    c.ltNoisePct = clampd(p.ltNoisePct, 0.0, 5.0);
    c.ltDampingS = clampd(p.ltDampingS, 0.0, 60.0);
    c.fcvStrokeS = clampd(p.fcvStrokeS, 0.0, 120.0);
    return c;
}

std::size_t pipeSteps(double deadTimeS)
{
    return static_cast<std::size_t>(std::llround(deadTimeS / kStepS));
}

double fcvFlow(double stemPct, double pumpPct)
{
    const double s = pumpPct / 100.0;
    const double p = kPumpMaxPressureBar * s * s;
    return kFcvMaxFlowM3s * (stemPct / 100.0) * std::sqrt(p / kPumpMaxPressureBar);
}

} // namespace

LoopModel::LoopModel(quint32 seed) : m_rng(seed) {}

void LoopModel::resizePipe(std::size_t n, double padValue)
{
    while (m_pipe.size() > n)
        m_pipe.pop_front();
    if (m_pipe.size() < n) {
        const double pad = m_pipe.empty() ? padValue : m_pipe.front();
        m_pipe.insert(m_pipe.begin(), n - m_pipe.size(), pad);
    }
}

void LoopModel::refreshPv()
{
    m_state.pvPct = std::clamp(m_lagPct + m_noisePct, 0.0, 100.0);
}

void LoopModel::reset(const LoopParams &params, double outPct)
{
    m_params = clampedParams(params);
    outPct = clampd(outPct, 0.0, 100.0);
    m_state = LoopState{};
    m_state.outPct = outPct;
    m_state.spPct = outPct;
    m_state.stemPct = outPct;

    const double steady = steadyLevelM(m_params, outPct);
    const double h = std::min(steady, kOverflowLevelM);
    m_state.levelM = h;
    m_state.pumpPressureBar = kPumpMaxPressureBar * std::pow(m_params.pumpSpeedPct / 100.0, 2);
    const double q = fcvFlow(outPct, m_params.pumpSpeedPct);
    m_state.qFcv = q;
    m_state.qIn = q;
    m_state.qHv = kHvCoefficient * (m_params.hvOpeningPct / 100.0) * std::sqrt(h);

    m_pipe.assign(pipeSteps(m_params.deadTimeS), q);
    m_carryS = 0.0;
    m_lagPct = 100.0 * h / kTankHeightM;
    m_noisePct = 0.0;
    m_noiseSteps = 0;
    refreshPv();
}

void LoopModel::step()
{
    LoopState &st = m_state;

    // Stem moves toward OUT at the stroke rate.
    if (m_params.fcvStrokeS <= 0.0) {
        st.stemPct = st.outPct;
    } else {
        const double maxMove = 100.0 / m_params.fcvStrokeS * kStepS;
        const double d = st.outPct - st.stemPct;
        st.stemPct += std::clamp(d, -maxMove, maxMove);
    }
    st.stemPct = std::clamp(st.stemPct, 0.0, 100.0);

    const double s = m_params.pumpSpeedPct / 100.0;
    st.pumpPressureBar = kPumpMaxPressureBar * s * s;
    st.qFcv = fcvFlow(st.stemPct, m_params.pumpSpeedPct);

    const std::size_t n = pipeSteps(m_params.deadTimeS);
    if (n == 0) {
        m_pipe.clear();
        st.qIn = st.qFcv;
    } else {
        resizePipe(n, st.qFcv);
        m_pipe.push_back(st.qFcv);
        st.qIn = m_pipe.front();
        m_pipe.pop_front();
    }

    st.qHv = kHvCoefficient * (m_params.hvOpeningPct / 100.0) * std::sqrt(std::max(st.levelM, 0.0));

    double h = st.levelM + (st.qIn - st.qHv) / kTankAreaM2 * kStepS;
    st.qOverflow = 0.0;
    if (h > kOverflowLevelM) {
        st.qOverflow = (h - kOverflowLevelM) * kTankAreaM2 / kStepS;
        h = kOverflowLevelM;
    }
    if (h < 0.0)
        h = 0.0;
    st.levelM = h;
    st.overflowing = st.qOverflow > 0.0;

    const double target = 100.0 * h / kTankHeightM;
    if (m_params.ltDampingS <= 0.0)
        m_lagPct = target;
    else
        m_lagPct += (target - m_lagPct) * (1.0 - std::exp(-kStepS / m_params.ltDampingS));

    const int noiseEvery = static_cast<int>(std::llround(kNoisePeriodS / kStepS));
    if (++m_noiseSteps >= noiseEvery) {
        m_noiseSteps = 0;
        const double u = static_cast<double>(m_rng()) / 4294967296.0; // [0, 1)
        m_noisePct = m_params.ltNoisePct * (2.0 * u - 1.0);
    }
    refreshPv();

    st.timeS += kStepS;
}

void LoopModel::advance(double seconds)
{
    if (!(seconds > 0.0))
        return;
    m_carryS += seconds;
    const double steps = std::floor(m_carryS / kStepS + 1e-9);
    m_carryS = std::max(0.0, m_carryS - steps * kStepS);
    const long long n = static_cast<long long>(steps);
    for (long long i = 0; i < n; ++i)
        step();
}

const LoopParams &LoopModel::params() const { return m_params; }

void LoopModel::setParams(const LoopParams &params)
{
    m_params = clampedParams(params);
    resizePipe(pipeSteps(m_params.deadTimeS), m_state.qFcv);
    if (m_params.deadTimeS <= 0.0)
        m_pipe.clear();
}

void LoopModel::setOut(double pct) { m_state.outPct = clampd(pct, 0.0, 100.0); }
void LoopModel::setSp(double pct) { m_state.spPct = clampd(pct, 0.0, 100.0); }

void LoopModel::setLevelM(double h)
{
    h = clampd(h, 0.0, kOverflowLevelM);
    m_state.levelM = h;
    m_lagPct = 100.0 * h / kTankHeightM;
    refreshPv();
}

void LoopModel::setStemPct(double pct) { m_state.stemPct = clampd(pct, 0.0, 100.0); }

void LoopModel::fillPipe()
{
    const double q = fcvFlow(m_state.stemPct, m_params.pumpSpeedPct);
    m_state.qFcv = q;
    m_pipe.assign(pipeSteps(m_params.deadTimeS), q);
}

const LoopState &LoopModel::state() const { return m_state; }

double LoopModel::steadyLevelM(const LoopParams &params, double outPct)
{
    const LoopParams p = clampedParams(params);
    const double inflow = fcvFlow(clampd(outPct, 0.0, 100.0), p.pumpSpeedPct);
    if (inflow <= 0.0)
        return 0.0;
    const double v = p.hvOpeningPct / 100.0;
    if (v <= 0.0)
        return std::numeric_limits<double>::infinity();
    const double x = inflow / (kHvCoefficient * v);
    return x * x;
}

// Stubs created by T220; implemented by T223.
void LoopModel::setPid(const PidSettings &pid) { m_pid = pid; }
const PidSettings &LoopModel::pid() const { return m_pid; }
void LoopModel::setAuto(bool on) { m_state.autoMode = on; }
bool LoopModel::isAuto() const { return m_state.autoMode; }

} // namespace mbloop
