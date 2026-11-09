#pragma once

#include <QList>
#include <QString>

namespace mb {

/// One simulation offered by Simulation > Load simulation... (v1.3.4).
struct SimulationEntry {
    QString id;           ///< Stable identifier, never shown ("loop101-step").
    QString name;         ///< Translated name shown in the list.
    QString description;  ///< Translated one-line description of the selected entry.
    QString scenarioFile; ///< Scenario file name (.mbloop) in the examples folder.
    QString projectFile;  ///< Matching project file name (.mbproj) in the examples folder.
};

/// The simulations supported by the Loop Simulator, in display order. Texts follow the installed
/// translator at call time.
QList<SimulationEntry> builtInSimulations();

} // namespace mb
