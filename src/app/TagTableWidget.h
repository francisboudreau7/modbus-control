#pragma once

#include <QList>
#include <QStringList>
#include <QWidget>

class QMenu;
class QTableView;

namespace mb {

class TagTableModel;

/// Tag table view plus a small button row: Add, Duplicate, Remove, Write selected...
/// Installs delegates: combo boxes for Area and Type, a spin box for Address, a colour
/// picker (QColorDialog) for Color. Remove asks for confirmation when more than one row is
/// selected. "Write selected..." is enabled when the selection is 2+ writable tags.
///
/// Column visibility: a right-click on the horizontal header opens a menu with one checkable
/// action per column, in column order, titled with the header text (checked = visible). The
/// Name action is checked and disabled (Name is always visible). After a separator: "Show all
/// columns" and "Compact view" (hides exactly compactHiddenColumns()). Every change made from
/// the menu emits hiddenColumnsChanged() once. The menu is built by headerMenu() (tests use it
/// without showing it).
class TagTableWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TagTableWidget(TagTableModel *model, QWidget *parent = nullptr);

    QTableView *view() const;
    TagTableModel *model() const;
    /// Selected rows, ascending.
    QList<int> selectedRows() const;

    /// Hides exactly the columns whose keys are listed (TagTableModel::columnKey); unknown keys
    /// and "name" are ignored, every other column is shown. Does not emit hiddenColumnsChanged().
    void setHiddenColumns(const QStringList &keys);
    /// Keys of the hidden columns, in column order.
    QStringList hiddenColumns() const;
    /// The header context menu described above (new each call, parented to this widget; the
    /// caller may exec() or delete it).
    QMenu *headerMenu();

signals:
    /// "Write selected..." pressed; rows as selectedRows().
    void writeSelectionRequested(const QList<int> &rows);
    /// The user changed the visible columns from the header menu.
    void hiddenColumnsChanged();

private:
    // Implementation-defined; the task may change the private section freely.
    TagTableModel *m_model = nullptr;
    QTableView *m_view = nullptr;
};

} // namespace mb
