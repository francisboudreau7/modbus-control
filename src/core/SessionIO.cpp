#include "SessionIO.h"

// Stub created by T100; implemented by T103.

namespace mb {

QByteArray sessionToJson(const Project &, const Recorder &, const SessionCursors &)
{
    return {};
}

bool sessionFromJson(const QByteArray &, Project &, Recorder &, SessionCursors &, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented");
    return false;
}

bool saveSession(const QString &, const Project &, const Recorder &, const SessionCursors &,
                 QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented");
    return false;
}

bool loadSession(const QString &, Project &, Recorder &, SessionCursors &, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented");
    return false;
}

} // namespace mb
