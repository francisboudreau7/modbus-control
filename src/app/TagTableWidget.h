#pragma once

#include <QList>
#include <QWidget>

class QTableView;

namespace mb {

class TagTableModel;

/// Tag table view plus a small button row: Add, Duplicate, Remove, Write selected...
/// Installs delegates: combo boxes for Area and Type, a spin box for Address, a colour
/// picker (QColorDialog) for Color. Remove asks for confirmation when more than one row is
/// selected. "Write selected..." is enabled when the selection is 2+ writable tags.
class TagTableWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TagTableWidget(TagTableModel *model, QWidget *parent = nullptr);

    QTableView *view() const;
    TagTableModel *model() const;
    /// Selected rows, ascending.
    QList<int> selectedRows() const;

signals:
    /// "Write selected..." pressed; rows as selectedRows().
    void writeSelectionRequested(const QList<int> &rows);

private:
    // Implementation-defined; the task may change the private section freely.
    TagTableModel *m_model = nullptr;
    QTableView *m_view = nullptr;
};

} // namespace mb
