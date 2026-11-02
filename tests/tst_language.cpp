// Language choice and translator loading (v1.3.4).
#include "Language.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace mb;

class TstLanguage : public QObject
{
    Q_OBJECT

private slots:
    void settingRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        QVERIFY(readUiLanguage(settings) == UiLanguage::System);
        for (UiLanguage l : {UiLanguage::English, UiLanguage::French, UiLanguage::System}) {
            writeUiLanguage(settings, l);
            QVERIFY(readUiLanguage(settings) == l);
        }
        QCOMPARE(settings.value(QStringLiteral("ui/language")).toString(), QStringLiteral("system"));
        settings.setValue(QStringLiteral("ui/language"), QStringLiteral("de"));
        QVERIFY(readUiLanguage(settings) == UiLanguage::System);
    }

    void resolve_data()
    {
        QTest::addColumn<int>("choice");
        QTest::addColumn<QStringList>("system");
        QTest::addColumn<QString>("expected");
        const int sys = int(UiLanguage::System);
        QTest::newRow("system fr-CA") << sys << QStringList{QStringLiteral("fr-CA"), QStringLiteral("en-US")} << "fr";
        QTest::newRow("system fr") << sys << QStringList{QStringLiteral("fr")} << "fr";
        QTest::newRow("system fr_FR") << sys << QStringList{QStringLiteral("fr_FR")} << "fr";
        QTest::newRow("system en first") << sys << QStringList{QStringLiteral("en-CA"), QStringLiteral("fr-CA")} << "en";
        QTest::newRow("system de") << sys << QStringList{QStringLiteral("de-DE")} << "en";
        QTest::newRow("system fry is not fr") << sys << QStringList{QStringLiteral("fry")} << "en";
        QTest::newRow("system empty") << sys << QStringList{} << "en";
        QTest::newRow("english on fr") << int(UiLanguage::English) << QStringList{QStringLiteral("fr-CA")} << "en";
        QTest::newRow("french on en") << int(UiLanguage::French) << QStringList{QStringLiteral("en-US")} << "fr";
    }
    void resolve()
    {
        QFETCH(int, choice);
        QFETCH(QStringList, system);
        QFETCH(QString, expected);
        QCOMPARE(resolveLanguage(UiLanguage(choice), system), expected);
    }

    void installAndSwitch()
    {
        QCOMPARE(currentLanguage(), QStringLiteral("en"));
        QVERIFY(installTranslators(QStringLiteral("fr")));
        QCOMPARE(currentLanguage(), QStringLiteral("fr"));
        // Qt's own catalog: standard buttons.
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Annuler"));

        QVERIFY(installTranslators(QStringLiteral("en")));
        QCOMPARE(currentLanguage(), QStringLiteral("en"));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Cancel"));

        QVERIFY(!installTranslators(QStringLiteral("de")));
        QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Cancel"));
    }
};

QTEST_GUILESS_MAIN(TstLanguage)
#include "tst_language.moc"
