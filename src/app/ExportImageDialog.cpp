#include "ExportImageDialog.h"

// Stub: implemented by its task (see TASKS.md).

namespace mb {

ExportImageDialog::ExportImageDialog(QwtPlot *plot, PlotCursors *cursors, const QString &readoutText,
                                     QWidget *parent)
    : QDialog(parent), m_plot(plot), m_cursors(cursors), m_readout(readoutText)
{
}
bool ExportImageDialog::render(QwtPlot *, PlotCursors *, const QString &, const QSize &, int, bool,
                               const QString &, QString *error)
{
    if (error)
        *error = QStringLiteral("Not implemented");
    return false;
}
void ExportImageDialog::accept() { QDialog::accept(); }

} // namespace mb