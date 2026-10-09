#pragma once

#include "Tag.h"

#include <QList>
#include <QWidget>

#include <optional>

class QLabel;
class QTableWidget;

namespace mb {

class Recorder;

/// Cursor readout.
/// Header labels: "t1 = 12.300 s", "t2 = 15.800 s", "Δt = 3.500 s", "1/Δt = 0.2857 Hz"
/// ("—" when a cursor is missing; 1/Δt "—" when Δt == 0).
/// Table: one row per plotted tag: Tag ("name [unit]", tag colour swatch), Y@C1, Y@C2, ΔY
/// (= Y@C2 - Y@C1). Values come from Recorder::sampleAt(); NaN or missing cursor -> "—".
/// Numbers use 6 significant digits.
class CursorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit CursorPanel(QWidget *parent = nullptr);

    void setRecorder(const Recorder *recorder); ///< not owned, may be null
    void setTags(const QList<Tag> &tags);
    void setCursors(std::optional<double> t1, std::optional<double> t2);

    /// Plain-text version of the readout (header line + one line per tag), used by the
    /// image export. Empty when neither cursor is set.
    QString readoutText() const;

public slots:
    /// Recomputes values (call after setCursors or when new data arrived).
    void refresh();

private:
    // Implementation-defined; the task may change the private section freely.
    const Recorder *m_recorder = nullptr;
    QList<Tag> m_tags;
    std::optional<double> m_t1;
    std::optional<double> m_t2;
    QLabel *m_header = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace mb
