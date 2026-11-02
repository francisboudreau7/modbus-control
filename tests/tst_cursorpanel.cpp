#include "CursorPanel.h"
#include "PlotCursors.h"
#include "Recorder.h"
#include "SampleBatch.h"
#include "Tag.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTableWidget>
#include <QToolButton>
#include <QtTest>

#include <optional>
#include <utility>

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

QLabel *labelOf(const CursorPanel &panel, const char *name)
{
    return panel.findChild<QLabel *>(QString::fromLatin1(name));
}

QString dtText(const CursorPanel &panel)
{
    return labelOf(panel, "deltaTLabel")->text();
}

QString freqText(const CursorPanel &panel)
{
    return labelOf(panel, "freqLabel")->text();
}

QString headerText(const CursorPanel &panel, int col)
{
    QTableWidgetItem *item = tableOf(panel)->horizontalHeaderItem(col);
    return item ? item->text() : QString();
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

const QString kDashText = QStringLiteral(u"—");

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
    void sixCursorsColumns();
    void pairCombosAndFallback();
    void pairChoiceChangesDelta();
    void removeButtonsEmitSignal();
    void noCursorsText();
    void cursorListShowsColoursAndTimes();
    void decimalPointIsDot();
    void bitTagsShowOnOff();
};

void TstCursorPanel::layoutContract()
{
    CursorPanel panel;
    QVERIFY(tableOf(panel));
    QTableWidget *table = tableOf(panel);
    QVERIFY(table->verticalHeader()->isHidden());
    QVERIFY(table->editTriggers() == QAbstractItemView::NoEditTriggers);

    // Two cursors: Tag, Y@C1, Y@C2, then the dY of C1 -> C2.
    panel.setCursors(1.0, 2.0);
    QCOMPARE(table->columnCount(), 4);
    QCOMPARE(headerText(panel, 0), QStringLiteral("Tag"));
    QCOMPARE(headerText(panel, 1), QStringLiteral("Y@C1"));
    QCOMPARE(headerText(panel, 2), QStringLiteral("Y@C2"));
    QCOMPARE(headerText(panel, 3), QString(QStringLiteral(u"ΔY C1→C2")));
    QCOMPARE(table->horizontalHeaderItem(1)->foreground().color(), PlotCursors::colorOf(1));
    QCOMPARE(table->horizontalHeaderItem(2)->foreground().color(), PlotCursors::colorOf(2));
    QVERIFY(panel.findChild<QComboBox *>(QStringLiteral("compareFromCombo")));
    QVERIFY(panel.findChild<QComboBox *>(QStringLiteral("compareToCombo")));
}

void TstCursorPanel::noCursors()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.show();
    panel.setRecorder(&rec);
    panel.setTags(standardTags());

    QCOMPARE(panel.readoutText(), QString());
    QVERIFY(labelOf(panel, "noCursorsLabel")->isVisible());
    QCOMPARE(labelOf(panel, "noCursorsLabel")->text(),
             QStringLiteral("No cursors. Use Place cursors on the Trend to place them."));

    // Only plotted tags are listed: A, C, D; no value columns.
    QCOMPARE(rowCount(panel), 3);
    QCOMPARE(tableOf(panel)->columnCount(), 1);
}

void TstCursorPanel::onlyFirstCursor()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(1.5, std::nullopt);

    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = —")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = —")));
    // Tag, Y@C1 only: the pair is incomplete, so there is no dY column.
    QCOMPARE(tableOf(panel)->columnCount(), 2);
    // t = 1.5 uses the row at t = 1.
    QCOMPARE(cell(panel, 0, 0), QStringLiteral("A [V]"));
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("20"));
    QCOMPARE(cell(panel, 1, 1), QStringLiteral("200"));

    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), qsizetype(4)); // cursor times + A, C, D
    QCOMPARE(lines.at(0), QStringLiteral("C1 = 1.500 s"));
    QCOMPARE(lines.at(1), QStringLiteral("A [V]: C1 20"));
}

void TstCursorPanel::bothCursors()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(1.5, 3.0);

    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 1.500 s")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = 0.6667 Hz")));
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("20"));
    QCOMPARE(cell(panel, 0, 2), QStringLiteral("40"));
    QCOMPARE(cell(panel, 0, 3), QStringLiteral("20"));
    QCOMPARE(cell(panel, 1, 1), QStringLiteral("200"));
    QCOMPARE(cell(panel, 1, 2), QStringLiteral("400"));
    QCOMPARE(cell(panel, 1, 3), QStringLiteral("200"));
    QCOMPARE(cell(panel, 2, 3), QStringLiteral("2"));

    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), qsizetype(5));
    QCOMPARE(lines.at(0), QStringLiteral("C1 = 1.500 s   C2 = 3.000 s"));
    QCOMPARE(lines.at(1), QString(QStringLiteral(u"Δt (C1→C2) = 1.500 s   1/Δt = 0.6667 Hz")));
    QCOMPARE(lines.at(2), QString(QStringLiteral(u"A [V]: C1 20   C2 40   ΔY 20")));
}

void TstCursorPanel::deltaSignNegative()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(3.0, 1.0);

    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = -2.000 s")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = 0.5 Hz")));
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("40"));
    QCOMPARE(cell(panel, 0, 2), QStringLiteral("20"));
    QCOMPARE(cell(panel, 0, 3), QStringLiteral("-20"));
    QCOMPARE(panel.readoutText().split(QLatin1Char('\n')).at(2),
             QString(QStringLiteral(u"A [V]: C1 40   C2 20   ΔY -20")));
}

void TstCursorPanel::zeroDeltaHasNoFrequency()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(2.0, 2.0);

    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 0.000 s")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = —")));
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
    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 0.500 s")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = 2 Hz")));
    QCOMPARE(cell(panel, 1, 1), kDashText);
    QCOMPARE(cell(panel, 1, 2), kDashText);
    QCOMPARE(cell(panel, 1, 3), kDashText);
    // A is unaffected.
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("30"));

    // Cursor before the first row: no value at all.
    panel.setCursors(-1.0, 0.5);
    QCOMPARE(cell(panel, 0, 1), kDashText);
    QCOMPARE(cell(panel, 0, 2), QStringLiteral("10"));
    QCOMPARE(cell(panel, 0, 3), kDashText);
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
    QCOMPARE(lines.at(2), QString(QStringLiteral(u"D: C1 6   C2 7   ΔY 1")));
    QCOMPARE(lines.at(3), QString(QStringLiteral(u"A [V]: C1 20   C2 30   ΔY 10")));
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
    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 3.500 s")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = 0.2857 Hz")));
    QCOMPARE(panel.readoutText().split(QLatin1Char('\n')).at(0),
             QStringLiteral("C1 = 12.300 s   C2 = 15.800 s"));
    // Values: 6 significant digits, last row at or before the cursor.
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("0.123457"));

    panel.setCursors(0.0, std::nullopt);
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("1.23457e+06"));
    QCOMPARE(panel.findChild<QLabel *>(QStringLiteral("cursorTime1"))->text(),
             QStringLiteral("0.000 s"));
}

void TstCursorPanel::nullRecorderShowsDashes()
{
    CursorPanel panel;
    panel.setRecorder(nullptr);
    panel.setTags(standardTags());
    panel.setCursors(1.5, 3.0);

    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 1.500 s")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = 0.6667 Hz")));
    QCOMPARE(cell(panel, 0, 1), kDashText);
    QCOMPARE(cell(panel, 0, 2), kDashText);
    QCOMPARE(cell(panel, 0, 3), kDashText);
    QCOMPARE(panel.readoutText().split(QLatin1Char('\n')).at(2),
             QString(QStringLiteral(u"A [V]: C1 —   C2 —   ΔY —")));
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
    QCOMPARE(lines.size(), qsizetype(4)); // cursor times + A, C, D
    QCOMPARE(lines.at(0), QStringLiteral("C2 = 2.000 s"));
    QCOMPARE(lines.at(3), QString(QStringLiteral(u"D: C2 7")));
}

// v1.3.4: a bit tag reads ON / OFF at each cursor and has no delta.
void TstCursorPanel::bitTagsShowOnOff()
{
    Recorder rec2;
    rec2.append(batch(0, {good(1, 10), good(5, 0)}));
    rec2.append(batch(1, {good(1, 20), good(5, 1)}));
    rec2.append(batch(2, {good(1, 30), good(5, 1)}));
    rec2.append(batch(3, {good(1, 40), timeout(5)}));
    Tag bit = makeTag(5, QStringLiteral("Pump"), QString(), QStringLiteral("#123456"), true);
    bit.area = Area::Coil;
    bit.type = ValueType::Bool;
    CursorPanel panel;
    panel.setRecorder(&rec2);
    panel.setTags({standardTags().first(), bit});

    panel.setCursors(0.5, 2.5);
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("10"));
    QCOMPARE(cell(panel, 0, 3), QStringLiteral("20")); // numeric tags still get a delta
    QCOMPARE(cell(panel, 1, 1), QStringLiteral("OFF"));
    QCOMPARE(cell(panel, 1, 2), QStringLiteral("ON"));
    QCOMPARE(cell(panel, 1, 3), QString());
    QCOMPARE(tableOf(panel)->columnCount(), 4);

    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.size(), qsizetype(4));
    QCOMPARE(lines.at(2), QString(QStringLiteral(u"A [V]: C1 10   C2 30   ΔY 20")));
    QCOMPARE(lines.at(3), QStringLiteral("Pump: C1 OFF   C2 ON"));

    // No Good sample before the cursor -> "—"; a failed last sample counts as missing.
    panel.setCursors(3.0, std::nullopt);
    QCOMPARE(cell(panel, 1, 1), kDashText);
}

void TstCursorPanel::sixCursorsColumns()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    QMap<int, double> c;
    for (int n = 1; n <= 6; ++n)
        c.insert(n, double(n) - 0.5); // 0.5 .. 5.5
    panel.setCursors(c);

    QTableWidget *table = tableOf(panel);
    QCOMPARE(table->columnCount(), 8); // Tag, six values, dY
    for (int n = 1; n <= 6; ++n) {
        QCOMPARE(headerText(panel, n), QStringLiteral("Y@C%1").arg(n));
        QCOMPARE(table->horizontalHeaderItem(n)->foreground().color(), PlotCursors::colorOf(n));
    }
    QCOMPARE(headerText(panel, 7), QString(QStringLiteral(u"ΔY C1→C2")));
    // A: t 0.5 -> 10, 1.5 -> 20, 2.5 -> 30, 3.5 -> 40, 4.5 and 5.5 -> 40
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("10"));
    QCOMPARE(cell(panel, 0, 3), QStringLiteral("30"));
    QCOMPARE(cell(panel, 0, 6), QStringLiteral("40"));
    QCOMPARE(cell(panel, 0, 7), QStringLiteral("10"));

    const QStringList lines = panel.readoutText().split(QLatin1Char('\n'));
    QCOMPARE(lines.at(0), QStringLiteral("C1 = 0.500 s   C2 = 1.500 s   C3 = 2.500 s   "
                                         "C4 = 3.500 s   C5 = 4.500 s   C6 = 5.500 s"));
    QCOMPARE(lines.at(2),
             QString(QStringLiteral(u"A [V]: C1 10   C2 20   C3 30   C4 40   C5 40   C6 40   ΔY 10")));

    // Fewer cursors again: the columns shrink.
    panel.setCursors(c.value(1), c.value(2));
    QCOMPARE(table->columnCount(), 4);
}

void TstCursorPanel::pairCombosAndFallback()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    auto *from = panel.findChild<QComboBox *>(QStringLiteral("compareFromCombo"));
    auto *to = panel.findChild<QComboBox *>(QStringLiteral("compareToCombo"));
    QVERIFY(from && to);

    QCOMPARE(panel.comparedPair(), std::make_pair(1, 2)); // default

    panel.setCursors(QMap<int, double>{{1, 0.5}, {2, 1.5}, {4, 3.0}});
    QCOMPARE(from->count(), 3);
    QCOMPARE(from->itemText(2), QStringLiteral("C4"));
    QCOMPARE(from->currentText(), QStringLiteral("C1"));
    QCOMPARE(to->currentText(), QStringLiteral("C2"));

    // Choose C2 -> C4.
    from->setCurrentIndex(1);
    to->setCurrentIndex(2);
    QCOMPARE(panel.comparedPair(), std::make_pair(2, 4));
    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 1.500 s")));
    QCOMPARE(headerText(panel, 4), QString(QStringLiteral(u"ΔY C2→C4")));
    QCOMPARE(cell(panel, 0, 4), QStringLiteral("20")); // 40 - 20

    // C1 disappears: the pair stays.
    panel.setCursors(QMap<int, double>{{2, 1.5}, {4, 3.0}});
    QCOMPARE(panel.comparedPair(), std::make_pair(2, 4));

    // C4 disappears: fall back to the two lowest existing cursors.
    panel.setCursors(QMap<int, double>{{2, 1.5}, {3, 2.0}, {5, 4.0}});
    QCOMPARE(panel.comparedPair(), std::make_pair(2, 3));
    QCOMPARE(from->currentText(), QStringLiteral("C2"));
    QCOMPARE(to->currentText(), QStringLiteral("C3"));

    // One cursor only: no pair, no dY column.
    panel.setCursors(QMap<int, double>{{3, 2.0}});
    QCOMPARE(tableOf(panel)->columnCount(), 2);
    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = —")));
}

void TstCursorPanel::pairChoiceChangesDelta()
{
    Recorder rec;
    fillStandardRecorder(rec);
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags(standardTags());
    panel.setCursors(QMap<int, double>{{1, 0.0}, {2, 1.0}, {3, 3.0}});
    auto *to = panel.findChild<QComboBox *>(QStringLiteral("compareToCombo"));
    to->setCurrentIndex(2); // C1 -> C3
    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 3.000 s")));
    QCOMPARE(freqText(panel), QString(QStringLiteral(u"1/Δt = 0.3333 Hz")));
    QCOMPARE(cell(panel, 0, 4), QStringLiteral("30")); // 40 - 10
    QVERIFY(panel.readoutText().contains(QString(QStringLiteral(u"Δt (C1→C3) = 3.000 s"))));
}

void TstCursorPanel::removeButtonsEmitSignal()
{
    CursorPanel panel;
    panel.show();
    panel.setCursors(QMap<int, double>{{1, 0.5}, {3, 2.0}});
    QSignalSpy spy(&panel, &CursorPanel::removeCursorRequested);

    QVERIFY(!panel.findChild<QToolButton *>(QStringLiteral("removeCursor2")));
    auto *b3 = panel.findChild<QToolButton *>(QStringLiteral("removeCursor3"));
    QVERIFY(b3);
    QCOMPARE(b3->toolTip(), QStringLiteral("Remove C3"));
    b3->click();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 3);

    panel.findChild<QToolButton *>(QStringLiteral("removeCursor1"))->click();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toInt(), 1);

    // The panel itself removes nothing: that is the owner's job.
    QVERIFY(panel.findChild<QToolButton *>(QStringLiteral("removeCursor3")));
}

void TstCursorPanel::noCursorsText()
{
    CursorPanel panel;
    panel.show();
    panel.setCursors(QMap<int, double>{{1, 1.0}});
    QVERIFY(!labelOf(panel, "noCursorsLabel")->isVisible());
    panel.setCursors(QMap<int, double>{});
    QVERIFY(labelOf(panel, "noCursorsLabel")->isVisible());
    QVERIFY(!panel.findChild<QToolButton *>(QStringLiteral("removeCursor1")));
    QCOMPARE(panel.readoutText(), QString());
}

void TstCursorPanel::cursorListShowsColoursAndTimes()
{
    CursorPanel panel;
    panel.setCursors(QMap<int, double>{{2, 15.8}, {5, 20.0}});
    QCOMPARE(panel.findChild<QLabel *>(QStringLiteral("cursorTime2"))->text(),
             QStringLiteral("15.800 s"));
    QCOMPARE(panel.findChild<QLabel *>(QStringLiteral("cursorTime5"))->text(),
             QStringLiteral("20.000 s"));
    QLabel *name = panel.findChild<QLabel *>(QStringLiteral("cursorName5"));
    QVERIFY(name);
    QCOMPARE(name->text(), QStringLiteral("C5"));
    QCOMPARE(name->palette().color(QPalette::WindowText), PlotCursors::colorOf(5));
    QVERIFY(!panel.findChild<QLabel *>(QStringLiteral("cursorTime1")));
}

void TstCursorPanel::decimalPointIsDot()
{
    const QLocale previous = QLocale();
    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
    Recorder rec;
    rec.append(batch(0, {good(9, 0.5)}));
    CursorPanel panel;
    panel.setRecorder(&rec);
    panel.setTags({makeTag(9, QStringLiteral("E"), QString(), QStringLiteral("#123456"), true)});
    panel.setCursors(0.5, 1.5);
    QCOMPARE(cell(panel, 0, 1), QStringLiteral("0.5"));
    QCOMPARE(dtText(panel), QString(QStringLiteral(u"Δt = 1.000 s")));
    QLocale::setDefault(previous);
}

QTEST_MAIN(TstCursorPanel)
#include "tst_cursorpanel.moc"
