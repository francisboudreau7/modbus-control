#include "DocsLinks.h"

#include <QtTest>

using namespace mb::docs;

class TstDocsLinks : public QObject
{
    Q_OBJECT
private slots:
    // Placeholder created by T200; T202 writes the real tests.
    void root()
    {
        QCOMPARE(siteRoot(), QStringLiteral("https://francisboudreau7.github.io/modbus-control/"));
    }
};

QTEST_MAIN(TstDocsLinks)
#include "tst_docslinks.moc"
