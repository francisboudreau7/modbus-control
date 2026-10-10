#pragma once

#include "CsvExporter.h"
#include "Tag.h"

#include <QDialog>
#include <QList>

#include <functional>
#include <optional>
#include <utility>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class TstExportCsvDialog; // test access to the warning seam

namespace mb {

class Recorder;

/// "Export data (CSV)" dialog: scope (All recorded data / Between cursors — disabled when
/// cursorRange is empty), delimiter (Comma / Semicolon), file path with Browse...
/// Columns: the given tags, in order, that the recorder has data for
/// (Recorder::tagIds()); header "name [unit]" or "name" when unit is empty.
/// OK writes through QSaveFile with exportCsv(); on failure shows a message and stays open.
class ExportCsvDialog : public QDialog
{
    Q_OBJECT
public:
    /// cursorRange: (min, max) of the two cursor times, if both cursors exist.
    ExportCsvDialog(const Recorder &recorder, const QList<Tag> &tags,
                    std::optional<std::pair<double, double>> cursorRange,
                    QWidget *parent = nullptr);

    void accept() override;

private:
    friend class ::TstExportCsvDialog;
    void updateState();

    /// Shows an error message; the constructor installs QMessageBox::warning (tests replace it).
    std::function<void(const QString &text)> m_warn;

    const Recorder &m_recorder;
    QList<Tag> m_tags;
    std::optional<std::pair<double, double>> m_range;

    QList<CsvColumn> m_columns;
    qsizetype m_allRows = 0;
    qsizetype m_rangeRows = 0;

    QRadioButton *m_allRadio = nullptr;
    QRadioButton *m_rangeRadio = nullptr;
    QComboBox *m_delimiter = nullptr;
    QLineEdit *m_file = nullptr;
    QLabel *m_info = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace mb
