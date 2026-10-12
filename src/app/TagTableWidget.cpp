#include "TagTableWidget.h"

#include "ProjectIO.h"
#include "TagTableModel.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColorDialog>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace mb {

namespace {

class ComboDelegate : public QStyledItemDelegate
{
public:
    ComboDelegate(const QStringList &items, QObject *parent)
        : QStyledItemDelegate(parent), m_items(items) {}

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *c = new QComboBox(parent);
        c->addItems(m_items);
        c->setFrame(false);
        c->setAutoFillBackground(true); // frameless editors are transparent in the Windows 11 style
        // Apply a pick immediately and close the editor: while it stays open the cell keeps
        // painting its old value under the editor's new one.
        auto *self = const_cast<ComboDelegate *>(this);
        connect(c, &QComboBox::activated, self, [self, c] {
            emit self->commitData(c);
            emit self->closeEditor(c);
        });
        QTimer::singleShot(0, c, &QComboBox::showPopup);
        return c;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *c = static_cast<QComboBox *>(editor);
        c->setCurrentText(index.data(Qt::EditRole).toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        model->setData(index, static_cast<QComboBox *>(editor)->currentText(), Qt::EditRole);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }

private:
    QStringList m_items;
};

class AddressDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        const auto *tm = qobject_cast<const TagTableModel *>(index.model());
        const int base = tm ? tm->addressBase() : 0;
        auto *s = new QSpinBox(parent);
        s->setRange(base, 65535 + base);
        s->setFrame(false);
        s->setAutoFillBackground(true);
        return s;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        static_cast<QSpinBox *>(editor)->setValue(index.data(Qt::EditRole).toInt());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        auto *s = static_cast<QSpinBox *>(editor);
        s->interpretText();
        model->setData(index, s->value(), Qt::EditRole);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }
};

/// Value cells: the default editor, loaded once. Live updates send dataChanged for the Value
/// column and the view would otherwise reset an open editor at every poll.
class ValueDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (editor->property("mcLoaded").toBool())
            return;
        editor->setProperty("mcLoaded", true);
        QStyledItemDelegate::setEditorData(editor, index);
    }
};

/// Quick A/B cells: a text editor, so an empty entry can clear the value (a spin box cannot
/// show "no value"). The model parses the text and refuses anything that is not a number.
class QuickDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *e = new QLineEdit(parent);
        e->setFrame(false);
        e->setAutoFillBackground(true);
        return e;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (editor->property("mcLoaded").toBool())
            return; // loaded once: never reset what the user is typing
        editor->setProperty("mcLoaded", true);
        static_cast<QLineEdit *>(editor)->setText(index.data(Qt::EditRole).toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override
    {
        model->setData(index, static_cast<QLineEdit *>(editor)->text(), Qt::EditRole);
    }
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }
};

/// Colour cells open a QColorDialog on double-click or when editing is triggered.
class ColorDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return nullptr;
    }
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override
    {
        const bool dbl = event->type() == QEvent::MouseButtonDblClick;
        bool key = false;
        if (event->type() == QEvent::KeyPress) {
            const int k = static_cast<QKeyEvent *>(event)->key();
            key = k == Qt::Key_F2 || k == Qt::Key_Return || k == Qt::Key_Enter;
        }
        if ((dbl || key) && (index.flags() & Qt::ItemIsEditable)) {
            const QColor initial(index.data(Qt::EditRole).toString());
            const QColor c = QColorDialog::getColor(initial, nullptr, tr("Tag colour"));
            if (c.isValid())
                model->setData(index, c.name(), Qt::EditRole);
            return true;
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};

class TagView : public QTableView
{
public:
    using QTableView::QTableView;
    std::function<void()> removeHandler;

protected:
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Delete && state() != EditingState && removeHandler) {
            removeHandler();
            e->accept();
            return;
        }
        QTableView::keyPressEvent(e);
    }
};

} // namespace

TagTableWidget::TagTableWidget(TagTableModel *model, QWidget *parent)
    : QWidget(parent), m_model(model)
{
    m_confirmRemove = [this](int count) {
        return QMessageBox::question(this, tr("Remove tags"),
                                     tr("Remove %n selected tag(s)?", nullptr, count))
               == QMessageBox::Yes;
    };
    auto *view = new TagView(this);
    m_view = view;
    m_view->setModel(model);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setVisible(false);
    m_view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view->horizontalHeader(), &QWidget::customContextMenuRequested, this, [this] {
        QMenu *menu = headerMenu();
        menu->exec(QCursor::pos());
        delete menu;
    });
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = m_view->indexAt(pos);
        if (!index.isValid())
            return;
        QMenu *menu = rowMenu(index.row());
        if (!menu)
            return;
        menu->exec(m_view->mapToGlobal(pos));
        delete menu;
    });
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::SelectedClicked);

    // Column order: Name, Area, Address, Type, ByteOrder, Gain, Offset, Unit, Writable, Plot,
    // Analyze, Axis, Color, Raw, Value, Min, Max, QuickA, QuickB, Quality.
    const int widths[TagTableModel::ColumnCount] = {140, 70, 70, 80, 70, 60, 60, 60, 70, 50,
                                                    60, 60, 100, 90, 90, 70, 70, 70, 70, 90};
    for (int c = 0; c < TagTableModel::ColumnCount; ++c)
        m_view->setColumnWidth(c, widths[c]);

    m_view->setItemDelegateForColumn(
        TagTableModel::Area,
        new ComboDelegate({toString(Area::HoldingRegister), toString(Area::InputRegister)}, this));
    m_view->setItemDelegateForColumn(
        TagTableModel::Type,
        new ComboDelegate({toString(ValueType::Int16), toString(ValueType::UInt16),
                           toString(ValueType::Int32), toString(ValueType::UInt32),
                           toString(ValueType::Float32)},
                          this));
    m_view->setItemDelegateForColumn(
        TagTableModel::ByteOrder,
        new ComboDelegate({toString(ByteOrder::ABCD), toString(ByteOrder::CDAB),
                           toString(ByteOrder::BADC), toString(ByteOrder::DCBA)},
                          this));
    m_view->setItemDelegateForColumn(
        TagTableModel::Axis,
        new ComboDelegate({toString(YAxis::Left), toString(YAxis::Right)}, this));
    m_view->setItemDelegateForColumn(TagTableModel::Address, new AddressDelegate(this));
    m_view->setItemDelegateForColumn(TagTableModel::Color, new ColorDelegate(this));
    m_view->setItemDelegateForColumn(TagTableModel::Value, new ValueDelegate(this));
    m_view->setItemDelegateForColumn(TagTableModel::WriteMin, new QuickDelegate(this));
    m_view->setItemDelegateForColumn(TagTableModel::WriteMax, new QuickDelegate(this));
    m_view->setItemDelegateForColumn(TagTableModel::QuickA, new QuickDelegate(this));
    m_view->setItemDelegateForColumn(TagTableModel::QuickB, new QuickDelegate(this));

    auto *addBtn = new QPushButton(tr("Add"), this);
    auto *dupBtn = new QPushButton(tr("Duplicate"), this);
    auto *remBtn = new QPushButton(tr("Remove"), this);
    auto *writeBtn = new QPushButton(tr("Write selected..."), this);
    writeBtn->setEnabled(false);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(addBtn);
    buttons->addWidget(dupBtn);
    buttons->addWidget(remBtn);
    buttons->addStretch(1);
    buttons->addWidget(writeBtn);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(buttons);
    layout->addWidget(m_view);

    auto removeSelected = [this] {
        const QList<int> rows = selectedRows();
        if (rows.isEmpty())
            return;
        if (rows.size() > 1 && !m_confirmRemove(int(rows.size())))
            return;
        // Remove from the bottom so row numbers stay valid.
        for (auto it = rows.crbegin(); it != rows.crend(); ++it)
            m_model->removeRows(*it, 1);
    };
    view->removeHandler = removeSelected;

    connect(addBtn, &QPushButton::clicked, this, [this] {
        const int row = m_model->addTag();
        const QModelIndex idx = m_model->index(row, TagTableModel::Name);
        m_view->setCurrentIndex(idx);
        m_view->scrollTo(idx);
    });
    connect(dupBtn, &QPushButton::clicked, this, [this] {
        const QList<int> rows = selectedRows();
        int last = -1;
        for (int r : rows)
            last = m_model->duplicateTag(r);
        if (last >= 0)
            m_view->setCurrentIndex(m_model->index(last, TagTableModel::Name));
    });
    connect(remBtn, &QPushButton::clicked, this, removeSelected);
    connect(writeBtn, &QPushButton::clicked, this,
            [this] { emit writeSelectionRequested(selectedRows()); });

    auto updateButtons = [this, dupBtn, remBtn, writeBtn] {
        const QList<int> rows = selectedRows();
        dupBtn->setEnabled(!rows.isEmpty());
        remBtn->setEnabled(!rows.isEmpty());
        bool ok = rows.size() >= 2;
        for (int r : rows)
            ok = ok && m_model->tagAt(r).isWritable();
        writeBtn->setEnabled(ok);
    };
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, updateButtons);
    connect(m_model, &QAbstractItemModel::dataChanged, this, updateButtons);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, updateButtons);
    connect(m_model, &QAbstractItemModel::modelReset, this, updateButtons);
    updateButtons();
}

QTableView *TagTableWidget::view() const { return m_view; }
TagTableModel *TagTableWidget::model() const { return m_model; }

QList<int> TagTableWidget::selectedRows() const
{
    QList<int> rows;
    const QModelIndexList sel = m_view->selectionModel()->selectedRows();
    for (const QModelIndex &i : sel)
        rows << i.row();
    std::sort(rows.begin(), rows.end());
    return rows;
}

void TagTableWidget::setHiddenColumns(const QStringList &keys)
{
    QList<bool> hide(TagTableModel::ColumnCount, false);
    m_unknownHidden.clear();
    for (const QString &key : keys) {
        const int c = TagTableModel::columnFromKey(key);
        // The Name column is always visible.
        if (c > TagTableModel::Name && c < TagTableModel::ColumnCount)
            hide[c] = true;
        else if (c < 0 && !m_unknownHidden.contains(key))
            m_unknownHidden << key;
    }
    for (int c = 0; c < TagTableModel::ColumnCount; ++c)
        m_view->setColumnHidden(c, hide[c]);
}

QStringList TagTableWidget::hiddenColumns() const
{
    QStringList keys;
    for (int c = 0; c < TagTableModel::ColumnCount; ++c) {
        if (m_view->isColumnHidden(c))
            keys << TagTableModel::columnKey(c);
    }
    return keys + m_unknownHidden;
}

void TagTableWidget::changeColumnsFromMenu(const std::function<void()> &change)
{
    const QStringList before = hiddenColumns();
    change();
    if (hiddenColumns() != before)
        emit hiddenColumnsChanged();
}

QMenu *TagTableWidget::headerMenu()
{
    auto *menu = new QMenu(this);

    for (int c = 0; c < TagTableModel::ColumnCount; ++c) {
        const QString title = m_model->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString();
        QAction *action = menu->addAction(title);
        action->setObjectName(QStringLiteral("columnAction_%1").arg(c));
        action->setCheckable(true);
        if (c == TagTableModel::Name) {
            action->setChecked(true);
            action->setEnabled(false);
            continue;
        }
        action->setChecked(!m_view->isColumnHidden(c));
        connect(action, &QAction::triggered, this, [this, c](bool visible) {
            changeColumnsFromMenu([this, c, visible] { m_view->setColumnHidden(c, !visible); });
        });
    }

    menu->addSeparator();

    QAction *showAll = menu->addAction(tr("Show all columns"));
    showAll->setObjectName(QStringLiteral("showAllColumnsAction"));
    connect(showAll, &QAction::triggered, this, [this] {
        changeColumnsFromMenu([this] {
            for (int c = 0; c < TagTableModel::ColumnCount; ++c)
                m_view->setColumnHidden(c, false);
        });
    });

    QAction *compact = menu->addAction(tr("Compact view"));
    compact->setObjectName(QStringLiteral("compactViewAction"));
    connect(compact, &QAction::triggered, this, [this] {
        changeColumnsFromMenu([this] {
            QStringList keys = compactHiddenColumns();
            keys += m_unknownHidden; // the menu only changes this version's columns
            setHiddenColumns(keys);
        });
    });

    return menu;
}

QMenu *TagTableWidget::rowMenu(int row)
{
    if (row < 0 || row >= m_model->rowCount())
        return nullptr;
    const Tag &tag = m_model->tagAt(row);
    const TagId id = tag.id;

    auto *menu = new QMenu(this);
    QAction *detect = menu->addAction(tr("Detect encoding..."));
    detect->setObjectName(QStringLiteral("detectEncodingAction"));
    detect->setEnabled(registerCount(tag.type) == 2 || !tag.encodingVerified);
    connect(detect, &QAction::triggered, this, [this, id] { emit detectEncodingRequested(id); });
    return menu;
}

} // namespace mb
