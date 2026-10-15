#include "Scenario.h"

// Stubs created by T210; implemented by T213.

namespace mbloop {

QByteArray scenarioToJson(const Scenario &)
{
    return {};
}

std::optional<Scenario> scenarioFromJson(const QByteArray &, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented yet.");
    return std::nullopt;
}

bool saveScenario(const QString &, const Scenario &, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented yet.");
    return false;
}

std::optional<Scenario> loadScenario(const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented yet.");
    return std::nullopt;
}

void applyScenario(LoopModel &model, const Scenario &scenario)
{
    model.setParams(scenario.params);
}

} // namespace mbloop
