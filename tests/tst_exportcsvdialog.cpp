#include "ExportCsvDialog.h"

#include "Recorder.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTime>
#include <QTimeZone>
#include <QtTest>

#include <initializer_list>
#include <optional>
#include <utility>

using namespace mb;

namespace {

QDateTime fixedStart()
{
    return QDateTime(QDate(2026, 10, 9), QTime(14, 3, 12, 250),
                     QTimeZone::fromSecondsAheadOfUtc(-4 * 3600));
}

TagSample good(TagId id, double value)
{
    TagSample s;
    s.tagId = id;
    s.value = value;
    s.quality = Quality::Good;
    return s;
}

SampleBatch batch(double t, std::initializer_list<TagSample> samples)
{
    SampleBatch b;
    b.t = t;
    for (const TagSample &s : samples)
        b.samples.append(s);
    return b;
}

// Tag 1 "A" [V], tag 2 "B" (no unit), tag 3 "C" [mA] which the recorder never sees.
QList<Tag> testTags()
{
    Tag a;
    a.id = 1;
    a.name = QStringLiteral("A");
    a.unit = QStringLiteral("V");
    Tag b;
    b.id = 2;
    b.name = QStringLiteral("B");
    Tag c;
    c.id = 3;
    c.name = QStringLiteral("C");
    c.unit = QStringLiteral("mA");
    return {a, b, c};
}

// Rows at t = 0, 1, 2, 3: A = 1..4, B = 10..40.
Recorder fourRows()
{
    Recorder recorder(100);
    recorder.setStartTime(fixedStart());
    for (int i = 0; i < 4; ++i)
        recorder.append(batch(i, {good(1, i + 1.0), good(2, (i + 1) * 10.0)}));
    return recorder;
}

void clearSettings()
{
    QSettings settings;
    settings.clear();
}

QString pathIn(const QTemporaryDir &dir, const QString &name)
{
    return QDir(dir.path()).filePath(name);
}

void setPath(ExportCsvDialog &dialog, const QString &path)
{
    dialog.findChild<QLineEdit *>()->setText(path);
}

QRadioButton *scopeRadio(const ExportCsvDialog &dialog, const QString &prefix)
{
    for (QRadioButton *radio : dialog.findChildren<QRadioButton *>()) {
        if (radio->text().startsWith(prefix))
            return radio;
    }
    return nullptr;
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

// Splits the CRLF-terminated lines into fields. The first row is the header.
QList<QStringList> parseRows(const QString &text, QChar delimiter)
{
    QList<QStringList> rows;
    for (const QString &line : text.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts))
        rows.append(line.split(delimiter));
    return rows;
}

} // namespace

// Note: the dialog's failure path (file in a missing directory) calls the static
// QMessageBox::warning(), which runs a modal event loop and would block the test, so it is
// not covered here. See the report for T142.
class TstExportCsvDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keep the dialog's QSettings writes away from the user's real settings.
        QCoreApplication::setOrganizationName(QStringLiteral("ModbusControlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_exportcsvdialog"));
        clearSettings();
    }

    void init() { clearSettings(); }

    void cleanupTestCase() { clearSettings(); }

    void allRowsWithCommaDelimiter()
    {
        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = pathIn(tmp, QStringLiteral("all.csv"));

        ExportCsvDialog dialog(recorder, tags, std::nullopt);
        QCOMPARE(dialog.findChild<QLabel *>()->text(), QStringLiteral("2 columns"));
        dialog.findChild<QComboBox *>()->setCurrentIndex(0);
        setPath(dialog, path);
        QRadioButton *all = scopeRadio(dialog, QStringLiteral("All recorded"));
        QVERIFY(all);
        QVERIFY(all->isChecked());

        dialog.accept();

        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(QFile::exists(path));
        const QString text = readText(path);
        const QList<QStringList> rows = parseRows(text, u',');
        QCOMPARE(rows.size(), qsizetype(5));
        // Header: unit appears in brackets, a unit-less tag has a bare name, and the tag the
        // recorder never saw is left out.
        QCOMPARE(rows.at(0).join(u'|'), QStringLiteral("timestamp|elapsed_s|A [V]|B"));
        QCOMPARE(rows.at(1).mid(1).join(u'|'), QStringLiteral("0.000|1|10"));
        QCOMPARE(rows.at(2).mid(1).join(u'|'), QStringLiteral("1.000|2|20"));
        QCOMPARE(rows.at(3).mid(1).join(u'|'), QStringLiteral("2.000|3|30"));
        QCOMPARE(rows.at(4).mid(1).join(u'|'), QStringLiteral("3.000|4|40"));
        // CRLF line endings only.
        QCOMPARE(text.count(u'\n'), qsizetype(5));
        QCOMPARE(text.count(QStringLiteral("\r\n")), qsizetype(5));
    }

    void semicolonDelimiter()
    {
        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = pathIn(tmp, QStringLiteral("semi.csv"));

        ExportCsvDialog dialog(recorder, tags, std::nullopt);
        dialog.findChild<QComboBox *>()->setCurrentIndex(1);
        setPath(dialog, path);

        dialog.accept();

        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(QFile::exists(path));
        const QString text = readText(path);
        QVERIFY(!text.contains(u','));
        const QList<QStringList> rows = parseRows(text, u';');
        QCOMPARE(rows.size(), qsizetype(5));
        QCOMPARE(rows.at(0).join(u'|'), QStringLiteral("timestamp|elapsed_s|A [V]|B"));
        QCOMPARE(rows.at(2).mid(1).join(u'|'), QStringLiteral("1.000|2|20"));
    }

    void rangeBetweenCursorsExportsOnlyInsideRange()
    {
        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = pathIn(tmp, QStringLiteral("range.csv"));

        // Cursors at 1 s and 2 s: both ends are inclusive, so two rows.
        ExportCsvDialog dialog(recorder, tags, std::pair<double, double>{1.0, 2.0});
        QRadioButton *range = scopeRadio(dialog, QStringLiteral("Between cursors"));
        QVERIFY(range);
        QVERIFY(range->isEnabled());
        QVERIFY(range->text().contains(QStringLiteral("2 rows")));
        range->setChecked(true);
        setPath(dialog, path);

        dialog.accept();

        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const QList<QStringList> rows = parseRows(readText(path), u',');
        QCOMPARE(rows.size(), qsizetype(3));
        QCOMPARE(rows.at(0).join(u'|'), QStringLiteral("timestamp|elapsed_s|A [V]|B"));
        QCOMPARE(rows.at(1).mid(1).join(u'|'), QStringLiteral("1.000|2|20"));
        QCOMPARE(rows.at(2).mid(1).join(u'|'), QStringLiteral("2.000|3|30"));
    }

    void rangeOptionAbsentWithoutCursors()
    {
        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        const QString path = pathIn(tmp, QStringLiteral("norange.csv"));

        ExportCsvDialog dialog(recorder, tags, std::nullopt);
        QRadioButton *range = scopeRadio(dialog, QStringLiteral("Between cursors"));
        QVERIFY(range);
        QVERIFY(!range->isEnabled());
        QCOMPARE(range->text(), QStringLiteral("Between cursors (place two cursors first)"));
        QVERIFY(scopeRadio(dialog, QStringLiteral("All recorded"))->isChecked());

        // Even if the range button is forced on, no cursor range means the whole recording.
        range->setChecked(true);
        QVERIFY(range->isChecked());
        setPath(dialog, pathIn(tmp, QStringLiteral("norange.csv")));

        dialog.accept();

        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const QList<QStringList> rows = parseRows(readText(path), u',');
        QCOMPARE(rows.size(), qsizetype(5));
        QCOMPARE(rows.at(4).mid(1).join(u'|'), QStringLiteral("3.000|4|40"));
    }

    void okButtonFollowsExportability()
    {
        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        ExportCsvDialog dialog(recorder, tags, std::nullopt);
        QPushButton *ok = dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        QVERIFY(ok);
        QVERIFY(ok->isEnabled());

        setPath(dialog, QStringLiteral("   "));
        QVERIFY(!ok->isEnabled());

        setPath(dialog, pathIn(tmp, QStringLiteral("ok.csv")));
        QVERIFY(ok->isEnabled());
    }

    void okDisabledWhenNothingToExport()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        // Empty recorder: no rows and no recorded columns.
        Recorder empty(100);
        empty.setStartTime(fixedStart());
        const QList<Tag> tags = testTags();
        ExportCsvDialog emptyDialog(empty, tags, std::nullopt);
        setPath(emptyDialog, pathIn(tmp, QStringLiteral("empty.csv")));
        QVERIFY(!emptyDialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());

        // Cursor range that holds no rows: OK only with the all-rows scope.
        const Recorder recorder = fourRows();
        ExportCsvDialog rangeDialog(recorder, tags, std::pair<double, double>{10.0, 20.0});
        QPushButton *ok = rangeDialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
        setPath(rangeDialog, pathIn(tmp, QStringLiteral("none.csv")));
        QVERIFY(ok->isEnabled());
        QRadioButton *range = scopeRadio(rangeDialog, QStringLiteral("Between cursors"));
        QVERIFY(range);
        QVERIFY(range->text().contains(QStringLiteral("0 rows")));
        range->setChecked(true);
        QVERIFY(!ok->isEnabled());
    }

    void appendsCsvSuffixWhenMissing()
    {
        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        ExportCsvDialog plain(recorder, tags, std::nullopt);
        setPath(plain, pathIn(tmp, QStringLiteral("report")));
        plain.accept();
        QCOMPARE(plain.result(), int(QDialog::Accepted));
        QVERIFY(QFile::exists(pathIn(tmp, QStringLiteral("report.csv"))));
        QVERIFY(!QFile::exists(pathIn(tmp, QStringLiteral("report"))));
        QCOMPARE(plain.findChild<QLineEdit *>()->text(), pathIn(tmp, QStringLiteral("report.csv")));

        // An existing .csv suffix is kept as typed, whatever its case.
        ExportCsvDialog upper(recorder, tags, std::nullopt);
        setPath(upper, pathIn(tmp, QStringLiteral("Upper.CSV")));
        upper.accept();
        QCOMPARE(upper.result(), int(QDialog::Accepted));
        QVERIFY(QFile::exists(pathIn(tmp, QStringLiteral("Upper.CSV"))));
        QVERIFY(!QFile::exists(pathIn(tmp, QStringLiteral("Upper.CSV.csv"))));
    }

    void savedDelimiterIsPreselected()
    {
        QSettings settings;
        settings.setValue(QStringLiteral("export/csvDelimiter"), QStringLiteral(";"));
        settings.sync();

        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        ExportCsvDialog dialog(recorder, tags, std::nullopt);
        QCOMPARE(dialog.findChild<QComboBox *>()->currentIndex(), 1);
    }

    void rememberedDelimiterAndDirectory()
    {
        const Recorder recorder = fourRows();
        const QList<Tag> tags = testTags();
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());

        {
            ExportCsvDialog first(recorder, tags, std::nullopt);
            first.findChild<QComboBox *>()->setCurrentIndex(1);
            setPath(first, pathIn(tmp, QStringLiteral("first.csv")));
            first.accept();
            QCOMPARE(first.result(), int(QDialog::Accepted));
        }

        // A new dialog starts with the semicolon delimiter and the folder of the last export.
        ExportCsvDialog second(recorder, tags, std::nullopt);
        QCOMPARE(second.findChild<QComboBox *>()->currentIndex(), 1);
        const QFileInfo suggested(second.findChild<QLineEdit *>()->text());
        QCOMPARE(suggested.fileName(), QStringLiteral("trend.csv"));
        QCOMPARE(suggested.absolutePath(), QDir(tmp.path()).absolutePath());
    }
};

QTEST_MAIN(TstExportCsvDialog)
#include "tst_exportcsvdialog.moc"
