#include "ClipboardCopy.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QClipboard>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QShortcut>

#include <algorithm>
#include <set>
#include <utility>

namespace mb {

namespace {

// Tabs and line breaks would corrupt the row/column structure, so each becomes one space.
QString sanitizeCell(QString text)
{
    for (QChar &ch : text) {
        if (ch == QChar('\t') || ch == QChar('\r') || ch == QChar('\n'))
            ch = QChar(' ');
    }
    return text;
}

} // namespace

QString tableToTsv(const QAbstractItemModel *model, const QModelIndexList &selection)
{
    if (!model)
        return {};

    QString out;

    if (selection.isEmpty()) {
        const int columns = model->columnCount();
        const int rows = model->rowCount();

        for (int c = 0; c < columns; ++c) {
            if (c > 0)
                out += QChar('\t');
            out += sanitizeCell(model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
        }
        out += QStringLiteral("\r\n");

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < columns; ++c) {
                if (c > 0)
                    out += QChar('\t');
                out += sanitizeCell(model->index(r, c).data(Qt::DisplayRole).toString());
            }
            out += QStringLiteral("\r\n");
        }
        return out;
    }

    // Bounding rectangle of the selection: every row and column from the smallest to the
    // largest selected one, so gaps between selected cells appear as empty cells.
    std::set<std::pair<int, int>> selected;
    int firstRow = 0;
    int lastRow = -1;
    int firstCol = 0;
    int lastCol = -1;
    for (const QModelIndex &index : selection) {
        if (!index.isValid())
            continue;
        if (selected.empty()) {
            firstRow = lastRow = index.row();
            firstCol = lastCol = index.column();
        }
        firstRow = std::min(firstRow, index.row());
        lastRow = std::max(lastRow, index.row());
        firstCol = std::min(firstCol, index.column());
        lastCol = std::max(lastCol, index.column());
        selected.emplace(index.row(), index.column());
    }

    for (int r = firstRow; r <= lastRow && !selected.empty(); ++r) {
        for (int c = firstCol; c <= lastCol; ++c) {
            if (c > firstCol)
                out += QChar('\t');
            if (selected.count({r, c}) > 0)
                out += sanitizeCell(model->index(r, c).data(Qt::DisplayRole).toString());
        }
        out += QStringLiteral("\r\n");
    }
    return out;
}

void installCopyShortcut(QAbstractItemView *view)
{
    if (!view)
        return;

    auto *shortcut = new QShortcut(QKeySequence(QKeySequence::Copy), view);
    shortcut->setContext(Qt::WidgetWithChildrenShortcut);
    QObject::connect(shortcut, &QShortcut::activated, view, [view] {
        QModelIndexList selection;
        if (QItemSelectionModel *selectionModel = view->selectionModel())
            selection = selectionModel->selectedIndexes();
        if (QClipboard *clipboard = QGuiApplication::clipboard())
            clipboard->setText(tableToTsv(view->model(), selection));
    });
}

} // namespace mb
