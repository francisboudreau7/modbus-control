#pragma once

#include "Tag.h"

#include <QHash>
#include <QWidget>

class QLabel;

namespace mb {

class TagTableModel;

/// Contents of the "Quick write" dock: one-click writes of preset values.
///
/// One row per tag of `model` that isWritable() and has quickA or quickB, in model order:
/// the tag name ("name [unit]"), its live Value (the model's Value column text, "—" when
/// empty), and a button per preset value: "A: <value>" / "B: <value>" (6 significant
/// digits, unit appended when set). A tag with only one preset gets only that button.
/// With no such tag the panel shows the hint "Set Quick A / Quick B on writable tags in the
/// tag table." Buttons are disabled while not connected.
///
/// The rows are rebuilt on the model's modelReset, rowsInserted, rowsRemoved and
/// tagsEdited; the live values follow dataChanged on the Value column without rebuilding.
/// Button objectNames: "quickA_<tagId>" and "quickB_<tagId>".
class QuickWritePanel : public QWidget
{
    Q_OBJECT
public:
    explicit QuickWritePanel(TagTableModel *model, QWidget *parent = nullptr);

public slots:
    void setConnected(bool connected);

signals:
    /// A button was clicked; connect to WriteController::requestQuickWrite.
    void quickWriteRequested(mb::TagId id, double scaledValue);

private:
    // Implementation-defined; the task may change the private section freely.
    void rebuild();
    void updateValue(int row);
    QString valueText(int row) const;

    TagTableModel *m_model = nullptr;
    bool m_connected = false;
    QWidget *m_content = nullptr;
    QHash<TagId, QLabel *> m_valueLabels;
};

} // namespace mb
