#pragma once

#include <QModelIndexList>
#include <QString>

class QAbstractItemModel;
class QAbstractItemView;

namespace mb {

/// Tab-separated text of table cells, for pasting into Excel or Word.
/// - Non-empty `selection`: the cells of the selection's bounding rectangle (rows and columns
///   sorted), selected cells with their DisplayRole text, unselected cells inside the
///   rectangle empty; no header row.
/// - Empty `selection`: a header row (horizontal headerData DisplayRole) and every cell.
/// Cells are separated by '\t', rows end with "\r\n" (also the last row). Tabs, CR and LF
/// inside a cell become single spaces. Hidden columns of `model` are not special-cased.
QString tableToTsv(const QAbstractItemModel *model, const QModelIndexList &selection);

/// Ctrl+C (QKeySequence::Copy) on `view` puts tableToTsv(view->model(), selected indexes)
/// on QGuiApplication::clipboard(). Uses a QShortcut with Qt::WidgetWithChildrenShortcut
/// parented to the view.
void installCopyShortcut(QAbstractItemView *view);

} // namespace mb
