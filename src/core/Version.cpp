#include "Version.h"

namespace mb {

QString versionString()
{
    return QStringLiteral(MBCORE_VERSION);
}

} // namespace mb