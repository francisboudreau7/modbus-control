#include "Version.h"

#include <QtTest>

class TstSmoke : public QObject
{
    Q_OBJECT
private slots:
    void versionIsNotEmpty()
    {
        QVERIFY(!mb::versionString().isEmpty());
        QCOMPARE(mb::versionString(), QStringLiteral("1.3.2"));
    }
};

QTEST_GUILESS_MAIN(TstSmoke)
#include "tst_smoke.moc"