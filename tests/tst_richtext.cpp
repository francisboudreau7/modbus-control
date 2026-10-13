#include "RichText.h"

#include <QtTest>

using namespace mb;

class TstRichText : public QObject
{
    Q_OBJECT
private slots:
    void escapesPlainText()
    {
        QCOMPARE(rich::headline(QStringLiteral("'a' < b & c")),
                 QStringLiteral("<p><b>'a' &lt; b &amp; c</b></p>"));
        QCOMPARE(rich::paragraph(QStringLiteral("x<y")), QStringLiteral("<p>x&lt;y</p>"));
        QCOMPARE(rich::paragraphRich(QStringLiteral("<b>x</b>")), QStringLiteral("<p><b>x</b></p>"));
        QCOMPARE(rich::note(QStringLiteral("raw 0x42")), QStringLiteral("<p><small>raw 0x42</small></p>"));
        QCOMPARE(rich::bold(QStringLiteral("1 < 2")), QStringLiteral("<b>1 &lt; 2</b>"));
    }

    void rowsAndGrid()
    {
        QCOMPARE(rich::rows({{QStringLiteral("Current value:"), QStringLiteral("50 °C")},
                             {QStringLiteral("New value:"), QStringLiteral("52.3 °C"), true}}),
                 QStringLiteral("<table cellspacing=\"0\" cellpadding=\"3\">"
                                "<tr><td>Current value:</td><td>50 °C</td></tr>"
                                "<tr><td>New value:</td><td><b>52.3 °C</b></td></tr></table>"));
        QCOMPARE(rich::grid({QStringLiteral("Tag"), QStringLiteral("New")},
                            {{QStringLiteral("A&B"), QStringLiteral("1")}}, 1),
                 QStringLiteral("<table cellspacing=\"0\" cellpadding=\"3\"><tr>"
                                "<th align=\"left\">Tag</th><th align=\"left\">New</th></tr>"
                                "<tr><td>A&amp;B</td><td><b>1</b></td></tr></table>"));
    }

    void bulletLists()
    {
        QCOMPARE(rich::bullets({QStringLiteral("a<"), QStringLiteral("b")}),
                 QStringLiteral("<ul><li>a&lt;</li><li>b</li></ul>"));
        QCOMPARE(rich::bulletsRich({QStringLiteral("<b>a</b>")}),
                 QStringLiteral("<ul><li><b>a</b></li></ul>"));
    }
};

QTEST_MAIN(TstRichText)
#include "tst_richtext.moc"
