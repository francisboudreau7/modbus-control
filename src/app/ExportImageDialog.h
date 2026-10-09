#pragma once

#include <QDialog>
#include <QSize>
#include <QString>

class QwtPlot;

namespace mb {

class PlotCursors;

/// "Export graph" dialog: file path with Browse... (format from the extension: .png, .svg,
/// .pdf), width/height in pixels (default = current plot size), DPI (default 96),
/// "Include cursors" (enabled when a cursor exists), "Include cursor readout" (enabled when
/// readoutText is non-empty). OK renders; on failure shows a message and stays open.
class ExportImageDialog : public QDialog
{
    Q_OBJECT
public:
    ExportImageDialog(QwtPlot *plot, PlotCursors *cursors, const QString &readoutText,
                      QWidget *parent = nullptr);

    /// Renders `plot` to `path` with QwtPlotRenderer. The cursor markers are hidden during
    /// rendering when !includeCursors (cursors may be null). A non-empty `readout` is drawn
    /// as a framed text box in the top-left corner of the canvas area. Returns false with
    /// an error on unknown extension or write failure.
    static bool render(QwtPlot *plot, PlotCursors *cursors, const QString &path,
                       const QSize &sizePx, int dpi, bool includeCursors,
                       const QString &readout, QString *error = nullptr);

    void accept() override;

private:
    // Implementation-defined; the task may change the private section freely.
    QwtPlot *m_plot = nullptr;
    PlotCursors *m_cursors = nullptr;
    QString m_readout;
};

} // namespace mb
