#pragma once

#include "Tag.h"

#include <QList>
#include <QStringList>
#include <QWidget>

#include <functional>

class QMenu;
class QTableView;
class TstTagTableModel; // test access to the remove confirmation seam

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
/// the menu emits hiddenColumnsChanged() once (not when it changes nothing). The menu is built by headerMenu() (tests use it
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

    /// Hides exactly the columns whose keys are listed (TagTableModel::columnKey); "name" is
    /// ignored, every other column is shown. Unknown keys (columns of a newer version) hide
    /// nothing but are kept and returned by hiddenColumns(), so saving does not drop them.
    /// Does not emit hiddenColumnsChanged().
    void setHiddenColumns(const QStringList &keys);
    /// Keys of the hidden columns, in column order, then the unknown keys last set.
    QStringList hiddenColumns() const;
    /// The header context menu described above (new each call, parented to this widget; the
    /// caller may exec() or delete it).
    QMenu *headerMenu();
    /// Context menu of a row (right-click on a cell; new each call, parented to this widget):
    /// "Detect encoding..." (object name "detectEncodingAction"), enabled only for 32-bit tags;
    /// it emits detectEncodingRequested(tag id). nullptr for an invalid row.
    QMenu *rowMenu(int row);

signals:
    /// "Write selected..." pressed; rows as selectedRows().
    void writeSelectionRequested(const QList<int> &rows);
    /// The user changed the visible columns from the header menu.
    void hiddenColumnsChanged();
    /// "Detect encoding..." chosen for a tag.
    void detectEncodingRequested(mb::TagId id);

private:
    // Implementation-defined; the task may change the private section freely.
    friend class ::TstTagTableModel;
    /// Asks before removing count (> 1) rows; the constructor installs a QMessageBox question.
    std::function<bool(int count)> m_confirmRemove;
    /// Keys from setHiddenColumns() that name no column of this version.
    QStringList m_unknownHidden;
    /// Applies a change made from the header menu; emits hiddenColumnsChanged() if it changed.
    void changeColumnsFromMenu(const std::function<void()> &change);
    TagTableModel *m_model = nullptr;
    QTableView *m_view = nullptr;
};

} // namespace mb
