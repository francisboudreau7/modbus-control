#include "SimulationCatalog.h"

#include <QCoreApplication>

namespace mb {

QList<SimulationEntry> builtInSimulations()
{
    return {
        {QStringLiteral("loop101-step"),
         QCoreApplication::translate("mb::SimulationCatalog", "Level loop LIC-101, step test"),
         QCoreApplication::translate("mb::SimulationCatalog",
                                     "Tank level loop with its regulator, starting at a steady level."),
         QStringLiteral("loop101-step.mbloop"), QStringLiteral("loop101.mbproj")},
    };
}

} // namespace mb
