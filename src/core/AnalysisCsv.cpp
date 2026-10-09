#include "AnalysisCsv.h"

// Stub created by T90; implemented by T93.

#include <QIODevice>

namespace mb {

bool exportAnalysisCsv(QIODevice &, const QList<StepResult> &, const QHash<TagId, QString> &,
                       QChar, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented");
    return false;
}

} // namespace mb
