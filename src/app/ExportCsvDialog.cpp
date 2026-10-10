#include "ExportCsvDialog.h"

#include "Recorder.h"

#include <utility>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace mb {

namespace {

const char kDelimiterKey[] = "export/csvDelimiter";
const char kDirKey[] = "export/csvDir";

QChar defaultDelimiter()
{
    // Excel in a comma-decimal locale expects ';' as the field separator.
    return QLocale().decimalPoint() == u',' ? u';' : u',';
}

QString withCsvSuffix(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || trimmed.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        return trimmed;
    return trimmed + QStringLiteral(".csv");
}

} // namespace

ExportCsvDialog::ExportCsvDialog(const Recorder &recorder, const QList<Tag> &tags,
                                 std::optional<std::pair<double, double>> cursorRange, QWidget *parent)
    : QDialog(parent), m_recorder(recorder), m_tags(tags), m_range(cursorRange)
{
    m_warn = [this](const QString &text) { QMessageBox::warning(this, windowTitle(), text); };
    setWindowTitle(tr("Export data (CSV)"));

    const QList<TagId> recorded = m_recorder.tagIds();
    for (const Tag &tag : std::as_const(m_tags)) {
        if (!recorded.contains(tag.id))
            continue;
        CsvColumn column;
        column.tagId = tag.id;
        column.header = tag.unit.isEmpty() ? tag.name : tag.name + QStringLiteral(" [") + tag.unit + QStringLiteral("]");
        m_columns.append(column);
    }

    m_allRows = m_recorder.size();
    if (m_range) {
        const auto [first, last] = m_recorder.rowRange(m_range->first, m_range->second);
        m_rangeRows = last - first;
    }

    // Scope
    m_allRadio = new QRadioButton(tr("All recorded data (%1 rows)").arg(m_allRows), this);
    m_rangeRadio = new QRadioButton(this);
    if (m_range) {
        m_rangeRadio->setText(QStringLiteral("Between cursors (%1 … %2 s, %3 rows)")
                                  .arg(QString::number(m_range->first, 'f', 3),
                                       QString::number(m_range->second, 'f', 3))
                                  .arg(m_rangeRows));
    } else {
        m_rangeRadio->setText(tr("Between cursors (place two cursors first)"));
        m_rangeRadio->setEnabled(false);
    }
    m_allRadio->setChecked(true);

    auto *scopeBox = new QWidget(this);
    auto *scopeLayout = new QVBoxLayout(scopeBox);
    scopeLayout->setContentsMargins(0, 0, 0, 0);
    scopeLayout->addWidget(m_allRadio);
    scopeLayout->addWidget(m_rangeRadio);

    // Delimiter
    QSettings settings;
    m_delimiter = new QComboBox(this);
    m_delimiter->addItem(tr("Comma (,)"));
    m_delimiter->addItem(tr("Semicolon (;)"));
    const QString savedDelimiter = settings.value(kDelimiterKey).toString();
    QChar delimiter = defaultDelimiter();
    if (savedDelimiter == QStringLiteral(","))
        delimiter = u',';
    else if (savedDelimiter == QStringLiteral(";"))
        delimiter = u';';
    m_delimiter->setCurrentIndex(delimiter == u';' ? 1 : 0);

    // File
    QString dir = settings.value(kDirKey).toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    m_file = new QLineEdit(QDir(dir).filePath(QStringLiteral("trend.csv")), this);
    auto *browse = new QPushButton(tr("Browse..."), this);
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(m_file, 1);
    fileRow->addWidget(browse);

    // Info
    m_info = new QLabel(tr("%1 columns").arg(m_columns.size()), this);

    auto *form = new QFormLayout;
    form->addRow(tr("Scope:"), scopeBox);
    form->addRow(tr("Delimiter:"), m_delimiter);
    form->addRow(tr("File:"), fileRow);
    form->addRow(tr("Columns:"), m_info);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &ExportCsvDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export data (CSV)"),
                                                          m_file->text(), tr("CSV file (*.csv)"));
        if (!path.isEmpty())
            m_file->setText(path);
    });
    connect(m_allRadio, &QRadioButton::toggled, this, [this] { updateState(); });
    connect(m_rangeRadio, &QRadioButton::toggled, this, [this] { updateState(); });
    connect(m_file, &QLineEdit::textChanged, this, [this] { updateState(); });

    updateState();
}

void ExportCsvDialog::updateState()
{
    const qsizetype rows = m_rangeRadio->isChecked() ? m_rangeRows : m_allRows;
    const bool ok = !m_file->text().trimmed().isEmpty() && rows > 0 && !m_columns.isEmpty();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

void ExportCsvDialog::accept()
{
    const QString path = withCsvSuffix(m_file->text());
    if (path.isEmpty())
        return;
    m_file->setText(path);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_warn(tr("Could not open %1 for writing:\n%2").arg(path, file.errorString()));
        return;
    }

    CsvOptions options;
    options.delimiter = m_delimiter->currentIndex() == 1 ? u';' : u',';
    options.useRange = m_rangeRadio->isChecked() && m_range.has_value();
    if (options.useRange) {
        options.t1 = m_range->first;
        options.t2 = m_range->second;
    }

    QString error;
    if (!exportCsv(file, m_recorder, m_columns, options, &error)) {
        file.cancelWriting();
        m_warn(tr("Could not export to %1:\n%2").arg(path, error));
        return;
    }
    if (!file.commit()) {
        m_warn(tr("Could not save %1:\n%2").arg(path, file.errorString()));
        return;
    }

    QSettings settings;
    settings.setValue(kDelimiterKey, QString(options.delimiter));
    settings.setValue(kDirKey, QFileInfo(path).absolutePath());

    QDialog::accept();
}

} // namespace mb
