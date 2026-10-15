#include "DocsLinks.h"

#include <QDir>

namespace mb::docs {

QString siteRoot()
{
    return QStringLiteral("https://francisboudreau7.github.io/modbus-control/");
}

Language languageFor(const QLocale &locale)
{
    return locale.language() == QLocale::French ? Language::French : Language::English;
}

QUrl siteUrl(Page page, Language language)
{
    const bool french = language == Language::French;
    QString path;
    if (page == Page::QuickStart)
        path = french ? QStringLiteral("fr/") : QString();
    else
        path = french ? QStringLiteral("fr/guide/") : QStringLiteral("guide/");
    return QUrl(siteRoot() + path);
}

QString localRelativePath(Page page, Language language)
{
    const bool french = language == Language::French;
    if (page == Page::QuickStart)
        return french ? QStringLiteral("docs/fr/GUIDE_DEMARRAGE_RAPIDE.md")
                      : QStringLiteral("docs/QUICK_START.md");
    return french ? QStringLiteral("docs/fr/GUIDE_UTILISATEUR.md")
                  : QStringLiteral("docs/USER_GUIDE.md");
}

QString localPath(const QString &appDir, Page page, Language language)
{
    const QDir base(appDir + QStringLiteral("/.."));
    return QDir::cleanPath(base.filePath(localRelativePath(page, language)));
}

} // namespace mb::docs
