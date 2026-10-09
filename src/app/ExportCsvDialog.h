#pragma once

#include "Tag.h"

#include <QDialog>
#include <QList>

#include <optional>
#include <utility>

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
    // Implementation-defined; the task may change the private section freely.
    const Recorder &m_recorder;
    QList<Tag> m_tags;
    std::optional<std::pair<double, double>> m_range;
};

} // namespace mb
