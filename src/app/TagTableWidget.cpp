#include "TagTableWidget.h"

#include "TagTableModel.h"

#include <QTableView>
#include <QVBoxLayout>

// Stub: implemented by its task (see TASKS.md).

namespace mb {

TagTableWidget::TagTableWidget(TagTableModel *model, QWidget *parent)
    : QWidget(parent), m_model(model), m_view(new QTableView(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_view);
    m_view->setModel(model);
}
QTableView *TagTableWidget::view() const { return m_view; }
TagTableModel *TagTableWidget::model() const { return m_model; }
QList<int> TagTableWidget::selectedRows() const { return {}; }

} // namespace mb