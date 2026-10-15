#include "LoopModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

// Stubs created by T210; implemented by T211.

namespace mbloop {

LoopModel::LoopModel(quint32 seed) : m_rng(seed) {}

void LoopModel::reset(const LoopParams &params, double outPct)
{
    m_params = params;
    m_state = LoopState{};
    m_state.outPct = outPct;
    m_state.spPct = outPct;
}

void LoopModel::advance(double seconds)
{
    m_state.timeS += seconds;
}

const LoopParams &LoopModel::params() const { return m_params; }
void LoopModel::setParams(const LoopParams &params) { m_params = params; }
void LoopModel::setOut(double pct) { m_state.outPct = pct; }
void LoopModel::setSp(double pct) { m_state.spPct = pct; }
void LoopModel::setLevelM(double h) { m_state.levelM = h; }
void LoopModel::setStemPct(double pct) { m_state.stemPct = pct; }
void LoopModel::fillPipe() {}
const LoopState &LoopModel::state() const { return m_state; }

double LoopModel::steadyLevelM(const LoopParams &, double)
{
    return 0.0;
}

} // namespace mbloop
