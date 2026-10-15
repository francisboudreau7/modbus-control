#pragma once

#include "LoopModel.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace mbloop {

/// A saved loop: settings and, optionally, the state to start from.
struct Scenario {
    LoopParams params;
    PidSettings pid;          ///< JSON "pid" (v1.3.1, see below)
    bool autoMode = false;    ///< JSON "pid.mode": "auto" | "manual"
    double speedFactor = 1.0; ///< 1, 2 or 5 (1 = real time)
    /// When present, the loop starts from these values instead of the steady state at OUT 50 %.
    struct State {
        double levelPct = 50.0; ///< 0..99
        double outPct = 50.0;   ///< 0..100
        double spPct = 50.0;    ///< 0..100
        double stemPct = 50.0;  ///< 0..100
        friend bool operator==(const State &, const State &) = default;
    };
    std::optional<State> state;
    friend bool operator==(const Scenario &, const Scenario &) = default;
};

/// JSON (.mbloop, UTF-8, indented):
/// { "format": "modbus-loop-scenario", "version": 1,
///   "settings": { "pumpSpeedPct": 100, "hvOpeningPct": 50, "deadTimeS": 2, "ltNoisePct": 0.05,
///                 "ltDampingS": 2, "fcvStrokeS": 5, "speedFactor": 1 },
///   "state": { "levelPct": 50, "outPct": 50, "spPct": 50, "stemPct": 50 } }   ("state" optional)
/// v1.3.1 (optional, additive): "pid": {"mode": "manual"|"auto", "kp": 1, "tiS": 30, "tdS": 0,
/// "action": "reverse"|"direct"}; ranges kp 0..100, tiS 0..3600, tdS 0..600; always written.
/// applyScenario also sets the PID settings and the mode (Auto entered bumplessly after the state).
/// Every key in "settings"/"state" is optional (defaults as in the structs); ranges: pump and HV
/// 0..100, deadTimeS 0..30, ltNoisePct 0..5, ltDampingS 0..60, fcvStrokeS 0..120, speedFactor one of
/// 1, 2, 5, levelPct 0..99, the others 0..100. Wrong format/version, unknown keys, wrong JSON
/// types or out-of-range values: nullopt and an error like "settings.deadTimeS: must be 0..30".
QByteArray scenarioToJson(const Scenario &scenario);
std::optional<Scenario> scenarioFromJson(const QByteArray &json, QString *error = nullptr);
/// File helpers; errors include the path.
bool saveScenario(const QString &path, const Scenario &scenario, QString *error = nullptr);
std::optional<Scenario> loadScenario(const QString &path, QString *error = nullptr);

/// Applies a scenario to a model: setParams, then reset(params, 50) and, when state is present,
/// setLevelM / setOut / setSp / setStemPct from it, then fillPipe().
void applyScenario(LoopModel &model, const Scenario &scenario);

} // namespace mbloop
