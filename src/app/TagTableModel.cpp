#include "TagTableModel.h"

#include <QBrush>
#include <QColor>

#include <algorithm>
#include <cmath>

namespace mb {

namespace {

const char *const kHeaders[TagTableModel::ColumnCount] = {
    QT_TRANSLATE_NOOP("TagTableModel", "Name"),    QT_TRANSLATE_NOOP("TagTableModel", "Area"),
    QT_TRANSLATE_NOOP("TagTableModel", "Address"), QT_TRANSLATE_NOOP("TagTableModel", "Type"),
    QT_TRANSLATE_NOOP("TagTableModel", "Gain"),    QT_TRANSLATE_NOOP("TagTableModel", "Offset"),
    QT_TRANSLATE_NOOP("TagTableModel", "Unit"),    QT_TRANSLATE_NOOP("TagTableModel", "Writable"),
    QT_TRANSLATE_NOOP("TagTableModel", "Plot"),    QT_TRANSLATE_NOOP("TagTableModel", "Color"),
    QT_TRANSLATE_NOOP("TagTableModel", "Raw"),     QT_TRANSLATE_NOOP("TagTableModel", "Value"),
    QT_TRANSLATE_NOOP("TagTableModel", "Quality"),
};

bool parseDouble(const QVariant &v, double &out)
{
    bool ok = false;
    if (v.metaType().id() == QMetaType::QString)
        out = v.toString().trimmed().toDouble(&ok);
    else
        out = v.toDouble(&ok);
    return ok && std::isfinite(out);
}

// Tableau 10. New tags take the first colour no existing tag uses.
const char *const kPalette[] = {"#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd",
                                "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf"};
constexpr int kPaletteSize = int(sizeof(kPalette) / sizeof(kPalette[0]));

QString nextPaletteColor(const QList<Tag> &existing)
{
    for (const char *candidate : kPalette) {
        const QString color = QLatin1String(candidate);
        const bool used = std::any_of(existing.cbegin(), existing.cend(), [&](const Tag &t) {
            return t.color.compare(color, Qt::CaseInsensitive) == 0;
        });
        if (!used)
            return color;
    }
    // All used: cycle.
    return QLatin1String(kPalette[existing.size() % kPaletteSize]);
}

} // namespace

TagTableModel::TagTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int TagTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_tags.size());
}

int TagTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TagTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tags.size()
        || index.column() < 0 || index.column() >= ColumnCount)
        return {};
    const Tag &t = m_tags.at(index.row());
    const Live live = m_live.value(t.id);
    const bool edit = role == Qt::EditRole;
    const bool disp = role == Qt::DisplayRole;

    switch (index.column()) {
    case Name:
        if (disp || edit) return t.name;
        break;
    case Area:
        if (disp || edit) return toString(t.area);
        break;
    case Address:
        if (disp || edit) return int(t.address);
        break;
    case Type:
        if (disp || edit) return toString(t.type);
        break;
    case Gain:
        if (disp || edit) return t.gain;
        break;
    case Offset:
        if (disp || edit) return t.offset;
        break;
    case Unit:
        if (disp || edit) return t.unit;
        break;
    case Writable:
        if (role == Qt::CheckStateRole)
            return t.isWritable() ? Qt::Checked : Qt::Unchecked;
        break;
    case Plot:
        if (role == Qt::CheckStateRole)
            return t.plotted ? Qt::Checked : Qt::Unchecked;
        break;
    case Color:
        if (disp || edit) return t.color;
        if (role == Qt::DecorationRole) return QColor(t.color);
        break;
    case Raw:
        if (disp)
            return live.valid ? QString::asprintf("0x%04X (%u)", uint(live.raw), uint(live.raw))
                              : QString();
        break;
    case Value:
        if (disp) return live.valid ? QString::number(live.value, 'g', 6) : QString();
        if (edit) return live.valid ? QVariant(live.value) : QVariant();
        break;
    case Quality:
        if (disp) return toString(live.quality);
        if (role == Qt::ForegroundRole) {
            switch (live.quality) {
            case mb::Quality::Good: return QBrush(QColor(0x2e, 0x7d, 0x32));
            case mb::Quality::Unknown: return QBrush(QColor(0x80, 0x80, 0x80));
            default: return QBrush(QColor(0xc6, 0x28, 0x28));
            }
        }
        if (role == Qt::ToolTipRole) return live.status;
        break;
    default:
        break;
    }
    return {};
}

bool TagTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tags.size())
        return false;
    Tag &t = m_tags[index.row()];
    const int col = index.column();

    if (col == Writable || col == Plot) {
        if (role != Qt::CheckStateRole)
            return false;
        const bool on = value.toInt() == Qt::Checked;
        if (col == Writable) {
            if (on && t.area == mb::Area::InputRegister)
                return false;
            t.writable = on;
        } else {
            t.plotted = on;
        }
        emit dataChanged(index, index, {Qt::CheckStateRole});
        emit tagsEdited();
        return true;
    }
    if (role != Qt::EditRole)
        return false;

    bool writableCleared = false;
    switch (col) {
    case Name: {
        const QString n = value.toString().trimmed();
        if (n.isEmpty()) return false;
        t.name = n;
        break;
    }
    case Area: {
        mb::Area a;
        const QString s = value.toString();
        if (s.compare(toString(mb::Area::HoldingRegister), Qt::CaseInsensitive) == 0)
            a = mb::Area::HoldingRegister;
        else if (s.compare(toString(mb::Area::InputRegister), Qt::CaseInsensitive) == 0)
            a = mb::Area::InputRegister;
        else
            return false;
        t.area = a;
        if (a == mb::Area::InputRegister && t.writable) {
            t.writable = false;
            writableCleared = true;
        }
        break;
    }
    case Address: {
        bool ok = false;
        const int a = value.toInt(&ok);
        if (!ok || a < 0 || a > 65535) return false;
        t.address = quint16(a);
        break;
    }
    case Type: {
        const QString s = value.toString();
        if (s.compare(toString(mb::ValueType::Int16), Qt::CaseInsensitive) == 0)
            t.type = mb::ValueType::Int16;
        else if (s.compare(toString(mb::ValueType::UInt16), Qt::CaseInsensitive) == 0)
            t.type = mb::ValueType::UInt16;
        else
            return false;
        break;
    }
    case Gain: {
        double g;
        if (!parseDouble(value, g) || g == 0.0) return false;
        t.gain = g;
        break;
    }
    case Offset: {
        double o;
        if (!parseDouble(value, o)) return false;
        t.offset = o;
        break;
    }
    case Unit:
        t.unit = value.toString();
        break;
    case Color: {
        const QString s = value.metaType().id() == QMetaType::QColor
                              ? value.value<QColor>().name()
                              : value.toString().trimmed();
        if (s.size() != 7 || !s.startsWith(QLatin1Char('#')) || !QColor::isValidColorName(s))
            return false;
        t.color = QColor(s).name(); // normalised lower-case #rrggbb
        break;
    }
    case Value: {
        if (!t.isWritable()) return false;
        double v;
        if (!parseDouble(value, v)) return false;
        emit writeRequested(t.id, v);
        return true;
    }
    default:
        return false;
    }

    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole, Qt::DecorationRole});
    if (writableCleared) {
        const QModelIndex i = this->index(index.row(), Writable);
        emit dataChanged(i, i, {Qt::CheckStateRole});
    }
    emit tagsEdited();
    return true;
}

Qt::ItemFlags TagTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_tags.size())
        return Qt::NoItemFlags;
    const Tag &t = m_tags.at(index.row());
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    switch (index.column()) {
    case Writable:
        f |= Qt::ItemIsUserCheckable;
        if (t.area == mb::Area::InputRegister)
            f &= ~Qt::ItemFlags(Qt::ItemIsEnabled);
        break;
    case Plot:
        f |= Qt::ItemIsUserCheckable;
        break;
    case Raw:
    case Quality:
        break;
    case Value:
        if (t.isWritable())
            f |= Qt::ItemIsEditable;
        break;
    default:
        f |= Qt::ItemIsEditable;
        break;
    }
    return f;
}

QVariant TagTableModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o == Qt::Horizontal && role == Qt::DisplayRole && section >= 0 && section < ColumnCount)
        return tr(kHeaders[section]);
    return QAbstractTableModel::headerData(section, o, role);
}

bool TagTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0 || row < 0 || row + count > m_tags.size())
        return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    for (int i = row; i < row + count; ++i)
        m_live.remove(m_tags.at(i).id);
    m_tags.remove(row, count);
    endRemoveRows();
    emit tagsEdited();
    return true;
}

void TagTableModel::setTags(const QList<Tag> &tags, TagId nextTagId)
{
    beginResetModel();
    m_tags = tags;
    m_live.clear();
    TagId next = nextTagId;
    for (const Tag &t : tags)
        next = std::max<TagId>(next, t.id + 1);
    m_nextId = std::max<TagId>(next, 1);
    endResetModel();
}

QList<Tag> TagTableModel::tags() const { return m_tags; }
TagId TagTableModel::nextTagId() const { return m_nextId; }
const Tag &TagTableModel::tagAt(int row) const { return m_tags.at(row); }

int TagTableModel::rowOf(TagId id) const
{
    for (int i = 0; i < m_tags.size(); ++i)
        if (m_tags.at(i).id == id)
            return i;
    return -1;
}

int TagTableModel::addTag(const Tag &prototype)
{
    Tag t = prototype;
    if (!m_tags.isEmpty() && prototype == Tag()) {
        const Tag &last = m_tags.last();
        t.area = last.area;
        t.address = last.address < 65535 ? quint16(last.address + 1) : quint16(65535);
    }
    if (t.color.compare(Tag().color, Qt::CaseInsensitive) == 0)
        t.color = nextPaletteColor(m_tags);
    t.id = m_nextId++;
    if (t.name.trimmed().isEmpty())
        t.name = QStringLiteral("Tag %1").arg(t.id);
    const int row = int(m_tags.size());
    beginInsertRows(QModelIndex(), row, row);
    m_tags.append(t);
    endInsertRows();
    emit tagsEdited();
    return row;
}

int TagTableModel::duplicateTag(int row)
{
    if (row < 0 || row >= m_tags.size())
        return -1;
    Tag t = m_tags.at(row);
    t.id = m_nextId++;
    t.name += QStringLiteral(" (copy)");
    const int newRow = int(m_tags.size());
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_tags.append(t);
    endInsertRows();
    emit tagsEdited();
    return newRow;
}

void TagTableModel::updateSamples(const SampleBatch &batch)
{
    QList<int> rows;
    for (const TagSample &s : batch.samples) {
        const int r = rowOf(s.tagId);
        if (r < 0)
            continue;
        Live &l = m_live[s.tagId];
        l.valid = s.quality == mb::Quality::Good;
        l.raw = s.raw;
        l.value = s.value;
        l.quality = s.quality;
        rows << r;
    }
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    const QList<int> roles{Qt::DisplayRole, Qt::ForegroundRole, Qt::ToolTipRole};
    int start = rows.first(), prev = start;
    for (int i = 1; i <= rows.size(); ++i) {
        if (i < rows.size() && rows.at(i) == prev + 1) {
            prev = rows.at(i);
            continue;
        }
        emit dataChanged(index(start, Raw), index(prev, Quality), roles);
        if (i < rows.size())
            start = prev = rows.at(i);
    }
}

void TagTableModel::clearLiveValues()
{
    m_live.clear();
    if (m_tags.isEmpty())
        return;
    emit dataChanged(index(0, Raw), index(int(m_tags.size()) - 1, Quality),
                     {Qt::DisplayRole, Qt::ForegroundRole, Qt::ToolTipRole});
}

void TagTableModel::setStatusText(TagId id, const QString &text)
{
    const int r = rowOf(id);
    if (r < 0)
        return;
    m_live[id].status = text;
    const QModelIndex i = index(r, Quality);
    emit dataChanged(i, i, {Qt::ToolTipRole});
}

} // namespace mb
