#include "TagTableModel.h"

// Stub: implemented by its task (see TASKS.md).

namespace mb {

TagTableModel::TagTableModel(QObject *parent) : QAbstractTableModel(parent) {}
int TagTableModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_tags.size()); }
int TagTableModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }
QVariant TagTableModel::data(const QModelIndex &, int) const { return {}; }
bool TagTableModel::setData(const QModelIndex &, const QVariant &, int) { return false; }
Qt::ItemFlags TagTableModel::flags(const QModelIndex &index) const { return QAbstractTableModel::flags(index); }
QVariant TagTableModel::headerData(int section, Qt::Orientation o, int role) const { return QAbstractTableModel::headerData(section, o, role); }
bool TagTableModel::removeRows(int, int, const QModelIndex &) { return false; }
void TagTableModel::setTags(const QList<Tag> &tags, TagId nextTagId) { beginResetModel(); m_tags = tags; m_nextId = nextTagId; endResetModel(); }
QList<Tag> TagTableModel::tags() const { return m_tags; }
TagId TagTableModel::nextTagId() const { return m_nextId; }
const Tag &TagTableModel::tagAt(int row) const { return m_tags.at(row); }
int TagTableModel::rowOf(TagId) const { return -1; }
int TagTableModel::addTag(const Tag &) { return -1; }
int TagTableModel::duplicateTag(int) { return -1; }
void TagTableModel::updateSamples(const SampleBatch &) {}
void TagTableModel::clearLiveValues() {}
void TagTableModel::setStatusText(TagId, const QString &) {}

} // namespace mb