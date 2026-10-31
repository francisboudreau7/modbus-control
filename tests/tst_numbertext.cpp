#include "NumberText.h"
#include "NumberSpinBox.h"

#include <QLineEdit>
#include <QtTest>

using namespace mb;

class TstNumberText : public QObject
{
    Q_OBJECT
private slots:
    void parse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("ok");
        QTest::addColumn<double>("value");
        QTest::newRow("point") << "72.3" << true << 72.3;
        QTest::newRow("comma") << "72,3" << true << 72.3;
        QTest::newRow("negative comma") << "-0,5" << true << -0.5;
        QTest::newRow("spaces") << "  12,5 " << true << 12.5;
        QTest::newRow("integer") << "42" << true << 42.0;
        QTest::newRow("exponent") << "1e-3" << true << 0.001;
        QTest::newRow("exponent comma") << "1,5E2" << true << 150.0;
        QTest::newRow("leading mark") << ",5" << true << 0.5;
        QTest::newRow("empty") << "" << false << 0.0;
        QTest::newRow("both marks") << "1,234.5" << false << 0.0;
        QTest::newRow("two commas") << "1,2,3" << false << 0.0;
        QTest::newRow("two points") << "1.2.3" << false << 0.0;
        QTest::newRow("group space") << "1 234" << false << 0.0;
        QTest::newRow("text") << "abc" << false << 0.0;
        QTest::newRow("inf") << "inf" << false << 0.0;
        QTest::newRow("nan") << "nan" << false << 0.0;
    }

    void parse()
    {
        QFETCH(QString, text);
        QFETCH(bool, ok);
        QFETCH(double, value);
        const std::optional<double> parsed = parseNumber(text);
        QCOMPARE(parsed.has_value(), ok);
        if (ok)
            QCOMPARE(*parsed, value);
        bool okOut = !ok;
        QCOMPARE(parseNumber(text, &okOut), ok ? value : 0.0);
        QCOMPARE(okOut, ok);
    }

    void numberTextUsesPoint()
    {
        QLocale::setDefault(QLocale(QLocale::French, QLocale::Canada));
        QCOMPARE(numberText(72.3), QStringLiteral("72.3"));
        QCOMPARE(numberText(1234567.0, 7), QStringLiteral("1234567"));
        QLocale::setDefault(QLocale::c());
    }

    void spinBoxAcceptsBothMarks()
    {
        QLocale::setDefault(QLocale(QLocale::French, QLocale::Canada));
        NumberSpinBox box;
        box.setRange(-1000, 1000);
        box.setDecimals(2);
        box.selectAll();
        QTest::keyClicks(&box, QStringLiteral("72,3"));
        box.interpretText();
        QCOMPARE(box.value(), 72.3);
        QCOMPARE(box.text(), QStringLiteral("72.30"));
        box.selectAll();
        QTest::keyClicks(&box, QStringLiteral("-4.5"));
        box.interpretText();
        QCOMPARE(box.value(), -4.5);
        QLocale::setDefault(QLocale::c());
    }

    void spinBoxSuffix()
    {
        NumberSpinBox box;
        box.setRange(0, 100);
        box.setDecimals(1);
        box.setSuffix(QStringLiteral(" s"));
        box.selectAll();
        QTest::keyClicks(&box, QStringLiteral("2,5"));
        box.interpretText();
        QCOMPARE(box.value(), 2.5);
        QCOMPARE(box.text(), QStringLiteral("2.5 s"));
    }
};

QTEST_MAIN(TstNumberText)
#include "tst_numbertext.moc"
