#include "DocsLinks.h"

// Stubs created by T200; implemented by T202.

namespace mb::docs {

QString siteRoot()
{
    return QStringLiteral("https://francisboudreau7.github.io/modbus-control/");
}

Language languageFor(const QLocale &)
{
    return Language::English;
}

QUrl siteUrl(Page, Language)
{
    return QUrl(siteRoot());
}

QString localRelativePath(Page, Language)
{
    return {};
}

QString localPath(const QString &, Page, Language)
{
    return {};
}

} // namespace mb::docs
