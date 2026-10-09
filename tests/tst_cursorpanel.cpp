#include "CursorPanel.h"
#include "Recorder.h"
#include "SampleBatch.h"
#include "Tag.h"

#include <QAbstractItemView>
#include <QColor>
#include <QHeaderView>
#include <QStringList>
#include <QLabel>
#include <QTableWidget>
#include <QtTest>

#include <optional>

using namespace mb;

namespace {

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

TagSample timeout(TagId id)
{
    TagSample s;
    s.tagId = id;
    s.value = 999.0; // must never be stored
    s.quality = Quality::Timeout;
    return s;
}

SampleBatch batch(double t, QList<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    b.samples = std::move(samples);
    return b;
}

Tag makeTag(TagId id, const QString &name, const QString &unit, const QString &color, bool plotted)
{
    Tag t;
    t.id = id;
    t.name = name;
    t.unit = unit;
    t.color = color;
    t.plotted = plotted;
    return t;
}

// Tags:  A "A" [V] red, plotted      B "B" hidden (not plotted)
//        C "C" [bar] green, plotted  D "D" (no unit) blue, plotted
// Rows (t = 0..3):
//   A: 10 20 30 40
//   B:  1  2  3  4
//   C: 100 200 <timeout> 400
//   D:  5  6  7  8
QList<Tag> standardTags()
{
    return {
        makeTag(1, QStringLiteral("A"), QStringLiteral("V"), QStringLiteral("#ff0000"), true),
        makeTag(2, QStringLiteral("B"), QString(), QStringLiteral("#00ff00"), false),
        makeTag(3, QStringLiteral("C"), QStringLiteral("bar"), QStringLiteral("#00aa00"), true),
        makeTag(4, QStringLiteral("D"), QString(), QStringLiteral("#0000ff"), true),
    };
}

void fillStandardRecorder(Recorder &r)
{
    r.append(batch(0, {good(1, 10), good(2, 1), good(3, 100), good(4, 5)}));
    r.append(batch(1, {good(1, 20), good(2, 2), good(3, 200), good(4, 6)}));
    r.append(batch(2, {good(1, 30), good(2, 3), timeout(3), good(4, 7)}));
    r.append(batch(3, {good(1, 40), good(2, 4), good(3, 400), good(4, 8)}));
}

QTableWidget *tableOf(const CursorPanel &panel)
{
    return panel.findChild<QTableWidget *>();
}

QLabel *headerOf(const CursorPanel &panel)
{
    return panel.findChild<QLabel *>();
}

QString cell(const CursorPanel &panel, int row, int col)
{
    QTableWidget *table = tableOf(panel);
    if (!table)
        return QStringLiteral("<no table>");
    QTableWidgetItem *item = table->item(row, col);
    return item ? item->text() : QString();
}

int rowCount(const CursorPanel &panel)
{
    QTableWidget *table = tableOf(panel);
    return table ? table->rowCount() : -1;
}

} // namespace

class TstCursorPanel : public QObject
{
    Q_OBJECT
private slots:
    void layoutContract();
    void noCursors();
    void onlyFirstCursor();
    void bothCursors();
    void deltaSignNegative();
    void zeroDeltaHasNoFrequency();
    void missingSampleIsDash();
    void hiddenTagNotListed();
    void tagOrderFollowsSetTags();
    void tagLabelsAndColours();
    void numberFormatting();
    void nullRecorderShowsDashes();
    void refreshPicksUpNewData();
    void readoutTextLines();
};

void TstCursorPanel::layoutContract()
{
    CursorPanel panel;
    QVERIFY(tableOf(panel));
    QVERIFY(headerOf(panel));
    QTableWidget *table = tableOf(panel);
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Tag"));
    QCOMPARE(table->horizontalHeaderItem(1)->text(), QStringLiteral("Y@C1"));
    QCOMPARE(table->horizontalHeaderItem(2)->text(), QStringLiteral("Y@C2"));
    QCOMPARE(table->horizontalHeaderItem(3)->text(), QString(QStringLiteral(u"ΔY")));
    QVERIFY(table->verticalHeader()->isHidden());
    QVERIFY(table->editTriggers() == QAbstractItemView::NoEditTriggers);
    QCOMPARE(table->horizontalHeader()->sectionResizeMode(0), QHeaderView::Stretch);
    QVERIFY(headerOf(panel)->wordWrap());
}

void TstCursorPanel::noCursors()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());

    QCOMPARE(panel.readoutText(), QString());
    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = —   t2 = —   Δt = —   1/Δt = —")));

    // Only plotted tags are listed: A, C, D.
    QCOMPARE(rowCount(panel), 3);
    QCOMPARE(cell(panel, 0, 1), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 0, 2), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 0, 3), QString(QStringLiteral(u"—")));
}

void TstCursorPanel::onlyFirstCursor()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(1.5, std::nullopt);

    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = 1.500 s   t2 = —   Δt = —   1/Δt = —")));
    // t = 1.5 uses the row at t = 1.
    QCOMPARE(cell(panel, 0, 0), QStringLiteral("A [V]"));
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("20"));
    QCOMPARE(cell(panel, 0, 2), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 0, 3), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 1, 1), QStringLiteral("200"));

    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), qsizetype(4));
    QCOMPARE(lines.at(0), headerOf(panel)->text());
    QCOMPARE(lines.at(1), QString(QStringLiteral(u"A [V]: Y1 = 20, Y2 = —, ΔY = —")));
}

void TstCursorPanel::bothCursors()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(1.5, 3.0);

    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = 1.500 s   t2 = 3.000 s   Δt = 1.500 s   1/Δt = 0.6667 Hz")));
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("20"));
    QCOMPARE(cell(panel, 0, 2), QStringLiteral("40"));
    QCOMPARE(cell(panel, 0, 3), QStringLiteral("20"));
    QCOMPARE(cell(panel, 1, 1), QStringLiteral("200"));
    QCOMPARE(cell(panel, 1, 2), QStringLiteral("400"));
    QCOMPARE(cell(panel, 1, 3), QStringLiteral("200"));
    QCOMPARE(cell(panel, 2, 3), QStringLiteral("2"));

    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), qsizetype(4));
    QCOMPARE(lines.at(1), QString(QStringLiteral(u"A [V]: Y1 = 20, Y2 = 40, ΔY = 20")));
}

void TstCursorPanel::deltaSignNegative()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(3.0, 1.0);

    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = 3.000 s   t2 = 1.000 s   Δt = -2.000 s   1/Δt = 0.5 Hz")));
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("40"));
    QCOMPARE(cell(panel, 0, 2), QStringLiteral("20"));
    QCOMPARE(cell(panel, 0, 3), QStringLiteral("-20"));
    QCOMPARE(panel.readoutText().split(QLatin1Char('\n')).at(1),
             QString(QStringLiteral(u"A [V]: Y1 = 40, Y2 = 20, ΔY = -20")));
}

void TstCursorPanel::zeroDeltaHasNoFrequency()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(2.0, 2.0);

    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = 2.000 s   t2 = 2.000 s   Δt = 0.000 s   1/Δt = —")));
    QCOMPARE(cell(panel, 0, 3), QStringLiteral("0"));
}

void TstCursorPanel::missingSampleIsDash()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());

    // C has a timeout (NaN) at t = 2; both cursors land on that row.
    panel.setCursors(2.0, 2.5);
    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = 2.000 s   t2 = 2.500 s   Δt = 0.500 s   1/Δt = 2 Hz")));
    QCOMPARE(cell(panel, 1, 1), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 1, 2), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 1, 3), QString(QStringLiteral(u"—")));
    // A is unaffected.
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("30"));

    // Cursor before the first row: no value at all.
    panel.setCursors(-1.0, 0.5);
    QCOMPARE(cell(panel, 0, 1), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 0, 2), QStringLiteral("10"));
    QCOMPARE(cell(panel, 0, 3), QString(QStringLiteral(u"—")));
}

void TstCursorPanel::hiddenTagNotListed()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(1.0, 2.0);

    QTableWidget *table = tableOf(panel);
    for (int r = 0; r < table->rowCount(); ++r)
        QVERIFY(table->item(r, 0)->text() != QStringLiteral("B"));
    QVERIFY(!panel.readoutText().contains(QStringLiteral("B:")));
    QVERIFY(!panel.readoutText().contains(QStringLiteral("B [")));
}

void TstCursorPanel::tagOrderFollowsSetTags()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);

    const QList<Tag> tags = standardTags();
    panel.setTags({tags.at(3), tags.at(0), tags.at(2)}); // D, A, C
    panel.setCursors(1.0, 2.0);

    QCOMPARE(rowCount(panel), 3);
    QCOMPARE(cell(panel, 0, 0), QStringLiteral("D"));
    QCOMPARE(cell(panel, 1, 0), QStringLiteral("A [V]"));
    QCOMPARE(cell(panel, 2, 0), QStringLiteral("C [bar]"));
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("6"));
    QCOMPARE(cell(panel, 1, 1), QStringLiteral("20"));

    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.at(1), QString(QStringLiteral(u"D: Y1 = 6, Y2 = 7, ΔY = 1")));
    QCOMPARE(lines.at(2), QString(QStringLiteral(u"A [V]: Y1 = 20, Y2 = 30, ΔY = 10")));
}

void TstCursorPanel::tagLabelsAndColours()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(0.0, 1.0);

    QCOMPARE(cell(panel, 0, 0), QStringLiteral("A [V]"));
    QCOMPARE(cell(panel, 1, 0), QStringLiteral("C [bar]"));
    QCOMPARE(cell(panel, 2, 0), QStringLiteral("D"));

    QTableWidget *table = tableOf(panel);
    QCOMPARE(table->item(0, 0)->data(Qt::DecorationRole).value<QColor>(), QColor(QStringLiteral("#ff0000")));
    QCOMPARE(table->item(1, 0)->data(Qt::DecorationRole).value<QColor>(), QColor(QStringLiteral("#00aa00")));
    QCOMPARE(table->item(2, 0)->data(Qt::DecorationRole).value<QColor>(), QColor(QStringLiteral("#0000ff")));

    // Numbers are right-aligned.
    QVERIFY(table->item(0, 1)->textAlignment() & Qt::AlignRight);
    QVERIFY(table->item(0, 3)->textAlignment() & Qt::AlignRight);
}

void TstCursorPanel::numberFormatting()
{
    Recorder rec;
    rec.append(batch(0, {good(9, 1234567.0)}));
    rec.append(batch(1, {good(9, 0.1234567)}));

    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags({makeTag(9, QStringLiteral("E"), QString(), QStringLiteral("#123456"), true)});
    panel.setCursors(12.3, 15.8);

    // Times: 3 decimals; frequency: 4 significant digits.
    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = 12.300 s   t2 = 15.800 s   Δt = 3.500 s   1/Δt = 0.2857 Hz")));
    // Values: 6 significant digits, last row at or before the cursor.
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("0.123457"));

    panel.setCursors(0.0, std::nullopt);
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("1.23457e+06"));
    QVERIFY(headerOf(panel)->text().startsWith(QString(QStringLiteral(u"t1 = 0.000 s   t2 = —"))));
}

void TstCursorPanel::nullRecorderShowsDashes()
{
    CursorPanel panel;
    panel.setRecorder(nullptr);
    panel.setTags(standardTags());
    panel.setCursors(1.5, 3.0);

    QCOMPARE(headerOf(panel)->text(),
             QString(QStringLiteral(u"t1 = 1.500 s   t2 = 3.000 s   Δt = 1.500 s   1/Δt = 0.6667 Hz")));
    QCOMPARE(cell(panel, 0, 1), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 0, 2), QString(QStringLiteral(u"—")));
    QCOMPARE(cell(panel, 0, 3), QString(QStringLiteral(u"—")));
    QCOMPARE(panel.readoutText().split(QLatin1Char('\n')).at(1),
             QString(QStringLiteral(u"A [V]: Y1 = —, Y2 = —, ΔY = —")));
}

void TstCursorPanel::refreshPicksUpNewData()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(5.0, std::nullopt);
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("40"));

    rec.append(batch(5, {good(1, 50)}));
    panel.refresh();
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("50"));
}

void TstCursorPanel::readoutTextLines()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());

    QCOMPARE(panel.readoutText(), QString());
    panel.setCursors(std::nullopt, 2.0);
    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), qsizetype(4)); // header + A, C, D
    QVERIFY(lines.at(0).startsWith(QString(QStringLiteral(u"t1 = —"))));
    QCOMPARE(lines.at(3), QString(QStringLiteral(u"D: Y1 = —, Y2 = 7, ΔY = —")));
}

QTEST_MAIN(TstCursorPanel)
#include "tst_cursorpanel.moc"
