#pragma once

#include <QtGlobal>

#include <deque>
#include <random>
#include <utility>

namespace mbsim {

enum class SignalKind : quint8 {
    Constant, ///< offset
    Sine,     ///< offset + amplitude * sin(2*pi*t / period)
    Triangle, ///< offset - amplitude at t=0, rises linearly to offset + amplitude at period/2, back down at period
    Sawtooth, ///< rises linearly from offset - amplitude (t=0) towards offset + amplitude, wraps every period
    Square,   ///< offset + amplitude for the first half of each period, offset - amplitude for the second
    Counter,  ///< starts at offset, +1 per tick, wraps 65535 -> 0 (raw uint16; amplitude/period ignored)
    Noise,    ///< offset + uniform random in [-amplitude, +amplitude], deterministic for a given seed
    Lag,      ///< first-order lag following `input` delayed by deadTimeS: y += (in(t - deadTimeS) - y) * (1 - exp(-dt / tau)); y starts at offset
};

struct SignalParams {
    SignalKind kind = SignalKind::Constant;
    double amplitude = 1000.0;
    double offset = 0.0;
    double periodS = 5.0;      ///< > 0
    double tauS = 2.0;         ///< Lag time constant, > 0
    quint16 sourceAddress = 0; ///< Lag: holding register providing `input` (int16)
    /// Lag: dead time in seconds, [0, 60]. The generator keeps the (t, input) history of
    /// the last deadTimeS seconds; before t >= deadTimeS the delayed input is the first one
    /// seen. 0 = no delay (the previous behaviour).
    double deadTimeS = 0.0;
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
    /// Same as next() but returns the value before rounding/clamping (Counter: the counter
    /// value 0..65535). Advances the state exactly like next(); call one or the other per tick.
    double nextValue(double t, double dt, quint16 input = 0);

    const SignalParams &params() const { return m_params; }

private:
    SignalParams m_params;
    std::mt19937 m_rng;
    double m_state = 0.0; ///< Lag output / Counter value
    bool m_started = false;
    /// Lag: (t, input) samples, oldest first. The front is the newest sample at or before
    /// t - deadTimeS (or the first sample seen if there is none); older samples are dropped.
    std::deque<std::pair<double, double>> m_history;
};

/// How a signal value is stored in the input registers.
enum class RegisterFormat : quint8 {
    Int16,   ///< 1 register: rounded, clamped to int16, two's complement
    Int32,   ///< 2 registers: rounded, clamped to int32, two's complement
    Float32, ///< 2 registers: IEEE-754 single precision
};

/// Order of the 4 bytes A (most significant) .. D of a 2-register value; same meaning as
/// the client's mb::ByteOrder: ABCD = reg n "A B", reg n+1 "C D"; CDAB = word swap;
/// BADC = byte swap within each register; DCBA = little endian.
enum class WordOrder : quint8 { ABCD, CDAB, BADC, DCBA };

inline int registerCount(RegisterFormat format)
{
    return format == RegisterFormat::Int16 ? 1 : 2;
}

/// Encodes `value` into registerCount(format) registers written to out[0] (lower address)
/// and, for 2-register formats, out[1]. Int16 ignores `order`.
void encodeRegisters(double value, RegisterFormat format, WordOrder order, quint16 out[2]);

} // namespace mbsim
