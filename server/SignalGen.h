#pragma once

#include <QtGlobal>

#include <random>

namespace mbsim {

enum class SignalKind : quint8 {
    Constant, ///< offset
    Sine,     ///< offset + amplitude * sin(2*pi*t / period)
    Triangle, ///< offset - amplitude at t=0, rises linearly to offset + amplitude at period/2, back down at period
    Sawtooth, ///< rises linearly from offset - amplitude (t=0) towards offset + amplitude, wraps every period
    Square,   ///< offset + amplitude for the first half of each period, offset - amplitude for the second
    Counter,  ///< starts at offset, +1 per tick, wraps 65535 -> 0 (raw uint16; amplitude/period ignored)
    Noise,    ///< offset + uniform random in [-amplitude, +amplitude], deterministic for a given seed
    Lag,      ///< first-order lag following `input`: y += (in - y) * (1 - exp(-dt / tau)); y starts at offset
};

struct SignalParams {
    SignalKind kind = SignalKind::Constant;
    double amplitude = 1000.0;
    double offset = 0.0;
    double periodS = 5.0;      ///< > 0
    double tauS = 2.0;         ///< Lag time constant, > 0
    quint16 sourceAddress = 0; ///< Lag: holding register providing `input` (int16)
};

/// Produces one raw register value per simulation tick. All kinds except Counter
/// work on int16 values: results are rounded to nearest, clamped to -32768..32767 and
/// returned as their two's-complement uint16 bit pattern.
class SignalGenerator
{
public:
    explicit SignalGenerator(const SignalParams &params, quint32 seed = 1);

    /// t: seconds since server start (>= 0, non-decreasing); dt: seconds since the
    /// previous call (0 on the first call); input: raw value of params.sourceAddress
    /// (Lag only, interpreted as int16).
    quint16 next(double t, double dt, quint16 input = 0);

    const SignalParams &params() const { return m_params; }

private:
    SignalParams m_params;
    std::mt19937 m_rng;
    double m_state = 0.0; ///< Lag output / Counter value
    bool m_started = false;
};

} // namespace mbsim
