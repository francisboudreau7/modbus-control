#include "SignalGen.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace mbsim {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// Rounds to nearest and clamps to [lo, hi]. NaN (e.g. from an infinite amplitude times
/// zero) becomes 0 so that the integer conversions below stay well defined.
double roundClamp(double value, double lo, double hi)
{
    if (std::isnan(value))
        return 0.0;
    return std::clamp(std::round(value), lo, hi);
}

/// Rounds to nearest, clamps to int16 and returns the two's-complement bit pattern.
quint16 toRegister(double value)
{
    const auto asInt16 = static_cast<qint16>(roundClamp(value, -32768.0, 32767.0));
    return static_cast<quint16>(asInt16);
}

/// Rounds to nearest, clamps to int32 and returns the two's-complement bit pattern.
quint32 toInt32Bits(double value)
{
    const auto asInt32 = static_cast<qint32>(roundClamp(value, -2147483648.0, 2147483647.0));
    return static_cast<quint32>(asInt32);
}

/// Wraps an arbitrary integer into the uint16 range 0..65535.
quint16 wrapToUInt16(long long value)
{
    long long wrapped = value % 65536;
    if (wrapped < 0)
        wrapped += 65536;
    return static_cast<quint16>(wrapped);
}

quint16 swapBytes(quint16 word)
{
    return static_cast<quint16>((word >> 8) | (word << 8));
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

double SignalGenerator::nextValue(double t, double dt, quint16 input)
{
    // Phase within the current period, in [0, 1).
    const double phase = std::fmod(t, m_params.periodS) / m_params.periodS;
    const double amp = m_params.amplitude;
    const double offset = m_params.offset;

    switch (m_params.kind) {
    case SignalKind::Constant:
        return offset;

    case SignalKind::Sine:
        return offset + amp * std::sin(2.0 * kPi * phase);

    case SignalKind::Triangle:
        if (phase < 0.5)
            return offset - amp + 4.0 * amp * phase;
        return offset + amp - 4.0 * amp * (phase - 0.5);

    case SignalKind::Sawtooth:
        return offset - amp + 2.0 * amp * phase;

    case SignalKind::Square:
        return phase < 0.5 ? offset + amp : offset - amp;

    case SignalKind::Counter: {
        // The first call returns the start value; each later call increments with wraparound.
        if (m_started)
            m_state = static_cast<double>(wrapToUInt16(static_cast<long long>(m_state) + 1));
        m_started = true;
        return m_state;
    }

    case SignalKind::Noise: {
        std::uniform_real_distribution<double> dist(-amp, amp);
        return dist(m_rng) + offset;
    }

    case SignalKind::Lag: {
        const double in = static_cast<double>(static_cast<qint16>(input));
        // Record this tick's input, then discard samples that can no longer be the delayed
        // value: the front moves on while the next sample is already at or before t - deadTimeS.
        m_history.emplace_back(t, in);
        const double delayedBefore = t - m_params.deadTimeS;
        while (m_history.size() > 1 && m_history[1].first <= delayedBefore)
            m_history.pop_front();
        const double delayedIn = m_history.front().second;

        if (dt > 0.0)
            m_state += (delayedIn - m_state) * (1.0 - std::exp(-dt / m_params.tauS));
        m_started = true;
        return m_state;
    }
    }

    return 0.0;
}

quint16 SignalGenerator::next(double t, double dt, quint16 input)
{
    const double value = nextValue(t, dt, input);
    if (m_params.kind == SignalKind::Counter)
        return static_cast<quint16>(value);
    return toRegister(value);
}

void encodeRegisters(double value, RegisterFormat format, WordOrder order, quint16 out[2])
{
    if (format == RegisterFormat::Int16) {
        out[0] = toRegister(value);
        return;
    }
    if (format == RegisterFormat::UInt16) {
        out[0] = static_cast<quint16>(roundClamp(value, 0.0, 65535.0));
        return;
    }

    // The 32-bit word is split into the high half (A B) and the low half (C D).
    quint32 bits = 0;
    switch (format) {
    case RegisterFormat::Int32:
        bits = toInt32Bits(value);
        break;
    case RegisterFormat::UInt32:
        bits = static_cast<quint32>(roundClamp(value, 0.0, 4294967295.0));
        break;
    default:
        bits = std::bit_cast<quint32>(static_cast<float>(value));
        break;
    }
    const auto hi = static_cast<quint16>(bits >> 16);
    const auto lo = static_cast<quint16>(bits & 0xFFFFu);

    switch (order) {
    case WordOrder::ABCD:
        out[0] = hi;
        out[1] = lo;
        break;
    case WordOrder::CDAB:
        out[0] = lo;
        out[1] = hi;
        break;
    case WordOrder::BADC:
        out[0] = swapBytes(hi);
        out[1] = swapBytes(lo);
        break;
    case WordOrder::DCBA:
        out[0] = swapBytes(lo);
        out[1] = swapBytes(hi);
        break;
    }
}

} // namespace mbsim
