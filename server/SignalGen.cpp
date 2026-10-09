#include "SignalGen.h"

#include <algorithm>
#include <cmath>

namespace mbsim {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Rounds to nearest, clamps to int16 and returns the two's-complement bit pattern.
quint16 toRegister(double value)
{
    const double rounded = std::round(value);
    const double clamped = std::clamp(rounded, -32768.0, 32767.0);
    const auto asInt16 = static_cast<qint16>(static_cast<int>(clamped));
    return static_cast<quint16>(asInt16);
}

/// Wraps an arbitrary integer into the uint16 range 0..65535.
quint16 wrapToUInt16(long long value)
{
    long long wrapped = value % 65536;
    if (wrapped < 0)
        wrapped += 65536;
    return static_cast<quint16>(wrapped);
}

} // namespace

SignalGenerator::SignalGenerator(const SignalParams &params, quint32 seed)
    : m_params(params)
    , m_rng(seed)
{
    if (m_params.kind == SignalKind::Lag)
        m_state = m_params.offset;
    else if (m_params.kind == SignalKind::Counter)
        m_state = static_cast<double>(wrapToUInt16(std::llround(m_params.offset)));
}

quint16 SignalGenerator::next(double t, double dt, quint16 input)
{
    // Phase within the current period, in [0, 1).
    const double phase = std::fmod(t, m_params.periodS) / m_params.periodS;
    const double amp = m_params.amplitude;
    const double offset = m_params.offset;

    switch (m_params.kind) {
    case SignalKind::Constant:
        return toRegister(offset);

    case SignalKind::Sine:
        return toRegister(offset + amp * std::sin(2.0 * kPi * phase));

    case SignalKind::Triangle:
        if (phase < 0.5)
            return toRegister(offset - amp + 4.0 * amp * phase);
        return toRegister(offset + amp - 4.0 * amp * (phase - 0.5));

    case SignalKind::Sawtooth:
        return toRegister(offset - amp + 2.0 * amp * phase);

    case SignalKind::Square:
        return toRegister(phase < 0.5 ? offset + amp : offset - amp);

    case SignalKind::Counter: {
        // The first call returns the start value; each later call increments with wraparound.
        if (m_started)
            m_state = static_cast<double>(wrapToUInt16(static_cast<long long>(m_state) + 1));
        m_started = true;
        return static_cast<quint16>(m_state);
    }

    case SignalKind::Noise: {
        std::uniform_real_distribution<double> dist(-amp, amp);
        return toRegister(dist(m_rng) + offset);
    }

    case SignalKind::Lag: {
        const double in = static_cast<double>(static_cast<qint16>(input));
        if (dt > 0.0)
            m_state += (in - m_state) * (1.0 - std::exp(-dt / m_params.tauS));
        m_started = true;
        return toRegister(m_state);
    }
    }

    return 0;
}

} // namespace mbsim
