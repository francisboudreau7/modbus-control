#include "DocsLinks.h"

#include <QtTest>

using namespace mb::docs;

class TstDocsLinks : public QObject
{
    Q_OBJECT
private slots:
    void root()
    {
        QCOMPARE(siteRoot(), QStringLiteral("https://francisboudreau7.github.io/modbus-control/"));
    }

    void siteUrls()
    {
        const QString root = siteRoot();
        QCOMPARE(siteUrl(Page::QuickStart, Language::English).toString(), root);
        QCOMPARE(siteUrl(Page::QuickStart, Language::French).toString(), root + QStringLiteral("fr/"));
        QCOMPARE(siteUrl(Page::UserGuide, Language::English).toString(), root + QStringLiteral("guide/"));
        QCOMPARE(siteUrl(Page::UserGuide, Language::French).toString(),
                 root + QStringLiteral("fr/guide/"));
    }

    void languages()
    {
        QCOMPARE(languageFor(QLocale(QLocale::French, QLocale::Canada)), Language::French);
        QCOMPARE(languageFor(QLocale(QLocale::French, QLocale::France)), Language::French);
        QCOMPARE(languageFor(QLocale(QLocale::English, QLocale::UnitedStates)), Language::English);
        QCOMPARE(languageFor(QLocale(QLocale::German, QLocale::Germany)), Language::English);
    }

    void localRelativePaths()
    {
        QCOMPARE(localRelativePath(Page::QuickStart, Language::English),
                 QStringLiteral("docs/QUICK_START.md"));
        QCOMPARE(localRelativePath(Page::QuickStart, Language::French),
                 QStringLiteral("docs/fr/GUIDE_DEMARRAGE_RAPIDE.md"));
        QCOMPARE(localRelativePath(Page::UserGuide, Language::English),
                 QStringLiteral("docs/USER_GUIDE.md"));
        QCOMPARE(localRelativePath(Page::UserGuide, Language::French),
                 QStringLiteral("docs/fr/GUIDE_UTILISATEUR.md"));
    }

    void localPathIsCleaned()
    {
        QCOMPARE(localPath(QStringLiteral("C:/x/bin"), Page::UserGuide, Language::English),
                 QStringLiteral("C:/x/docs/USER_GUIDE.md"));
        QCOMPARE(localPath(QStringLiteral("C:/x/bin/"), Page::QuickStart, Language::French),
                 QStringLiteral("C:/x/docs/fr/GUIDE_DEMARRAGE_RAPIDE.md"));
    }
};

QTEST_MAIN(TstDocsLinks)
#include "tst_docslinks.moc"
