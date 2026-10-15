#pragma once

#include <QLocale>
#include <QString>
#include <QUrl>

namespace mb {

/// Where the documentation lives: the website built from docs/ (GitHub Pages) and the copies the
/// installer puts next to the application.
namespace docs {

enum class Page { QuickStart, UserGuide };
enum class Language { English, French };

/// The site's root, "https://francisboudreau7.github.io/modbus-control/".
QString siteRoot();

/// French when `locale`'s language is French (any country), else English.
Language languageFor(const QLocale &locale);

/// Web page of `page` in `language`:
/// QuickStart: <root> (English) / <root>fr/ (French);
/// UserGuide: <root>guide/ (English) / <root>fr/guide/ (French).
QUrl siteUrl(Page page, Language language);

/// Installed copy, relative to the installation folder (the parent of the folder holding the
/// executable): "docs/QUICK_START.md", "docs/USER_GUIDE.md", "docs/fr/GUIDE_DEMARRAGE_RAPIDE.md",
/// "docs/fr/GUIDE_UTILISATEUR.md".
QString localRelativePath(Page page, Language language);

/// Absolute path of the installed copy for an executable in `appDir` (QCoreApplication::
/// applicationDirPath()): <appDir>/../<localRelativePath>, cleaned. The file may not exist.
QString localPath(const QString &appDir, Page page, Language language);

} // namespace docs
} // namespace mb
