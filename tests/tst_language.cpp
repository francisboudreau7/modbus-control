// Language choice and translator loading (v1.3.4).
#include "Language.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
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

    void frenchCatalogTranslatesOurTexts()
    {
        QVERIFY(installTranslators(QStringLiteral("fr")));
        QCOMPARE(QCoreApplication::translate("mb::MainWindow", "&File"), QStringLiteral("&Fichier"));
        QCOMPARE(QCoreApplication::translate("mb::Tag", "Coil (FC01/FC05/FC15)"),
                 QStringLiteral("Bobine (FC01/FC05/FC15)"));
        QVERIFY(installTranslators(QStringLiteral("en")));
        QCOMPARE(QCoreApplication::translate("mb::MainWindow", "&File"), QStringLiteral("&File"));
    }

    // Every text extracted by lupdate (update_translations target) has a finished translation,
    // and no entry is left over from removed texts.
    void catalogsAreComplete_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("fr") << QStringLiteral(MC_TRANSLATIONS_DIR "/modbus_control_fr.ts");
        QTest::newRow("en") << QStringLiteral(MC_TRANSLATIONS_DIR "/modbus_control_en.ts");
    }
    void catalogsAreComplete()
    {
        QFETCH(QString, file);
        QFile f(file);
        QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(file));
        const QString ts = QString::fromUtf8(f.readAll());
        QVERIFY(ts.contains(QLatin1String("<message")));
        static const QRegularExpression stale(QStringLiteral(R"re(type="(unfinished|obsolete|vanished)")re"));
        QStringList problems;
        static const QRegularExpression message(QStringLiteral(R"re(<message[^>]*>.*?</message>)re"),
                                                QRegularExpression::DotMatchesEverythingOption);
        for (auto it = message.globalMatch(ts); it.hasNext();) {
            const QString m = it.next().captured();
            if (stale.match(m).hasMatch())
                problems << m.section(QLatin1String("<source>"), 1).section(QLatin1String("</source>"), 0, 0);
        }
        QVERIFY2(problems.isEmpty(), qPrintable(QStringLiteral("%1 entries not translated: %2")
                                                    .arg(problems.size())
                                                    .arg(problems.mid(0, 10).join(QLatin1String(" | ")))));
    }
};

QTEST_GUILESS_MAIN(TstLanguage)
#include "tst_language.moc"
