#include "ExportCsvDialog.h"

// Stub: implemented by its task (see TASKS.md).

namespace mb {

ExportCsvDialog::ExportCsvDialog(const Recorder &recorder, const QList<Tag> &tags,
                                 std::optional<std::pair<double, double>> cursorRange, QWidget *parent)
    : QDialog(parent), m_recorder(recorder), m_tags(tags), m_range(cursorRange)
{
}
void ExportCsvDialog::accept() { QDialog::accept(); }

} // namespace mb