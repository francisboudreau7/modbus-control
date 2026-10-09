#include "CursorPanel.h"

#include "Recorder.h"

#include <QAbstractItemView>
#include <QColor>
#include <QHeaderView>
#include <QLabel>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace mb {

namespace {

const QString kDash = QStringLiteral(u"—");  // em dash: missing value
const QString kDelta = QStringLiteral(u"Δ"); // capital delta

constexpr int kColumnCount = 4;
constexpr int kColTag = 0;

double nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

QString fmtNum(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 6) : kDash;
}

QString fmtTime(std::optional<double> t)
{
    return t ? QString::number(*t, 'f', 3) + QStringLiteral(" s") : kDash;
}

QString headerLine(std::optional<double> t1, std::optional<double> t2)
{
    QString dt = kDash;
    QString hz = kDash;
    if (t1 && t2) {
        const double d = *t2 - *t1;
        dt = QString::number(d, 'f', 3) + QStringLiteral(" s");
        if (d != 0.0)
            hz = QString::number(1.0 / std::abs(d), 'g', 4) + QStringLiteral(" Hz");
    }
    return QStringLiteral("t1 = ") + fmtTime(t1)
         + QStringLiteral("   t2 = ") + fmtTime(t2)
         + QStringLiteral("   ") + kDelta + QStringLiteral("t = ") + dt
         + QStringLiteral("   1/") + kDelta + QStringLiteral("t = ") + hz;
}

QString tagLabel(const Tag &tag)
{
    return tag.unit.isEmpty() ? tag.name
                              : tag.name + QStringLiteral(" [") + tag.unit + QStringLiteral("]");
}

double valueAtCursor(const Recorder *recorder, TagId id, std::optional<double> t)
{
    if (!recorder || !t)
        return nan();
    return recorder->sampleAt(id, *t);
}

struct RowValues
{
    QString label;
    QString y1;
    QString y2;
    QString dy;
};

RowValues valuesFor(const Recorder *recorder, const Tag &tag,
                    std::optional<double> t1, std::optional<double> t2)
{
    const double y1 = valueAtCursor(recorder, tag.id, t1);
    const double y2 = valueAtCursor(recorder, tag.id, t2);
    RowValues v;
    v.label = tagLabel(tag);
    v.y1 = fmtNum(y1);
    v.y2 = fmtNum(y2);
    v.dy = fmtNum(y2 - y1); // NaN propagates when either side is missing
    return v;
}

} // namespace

CursorPanel::CursorPanel(QWidget *parent) : QWidget(parent)
{
    m_header = new QLabel(this);
    m_header->setWordWrap(true);
    m_header->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_table = new QTableWidget(0, kColumnCount, this);
    m_table->setHorizontalHeaderLabels(QStringList{
        QStringLiteral("Tag"),
        QStringLiteral("Y@C1"),
        QStringLiteral("Y@C2"),
        kDelta + QStringLiteral("Y"),
    });
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(kColTag, QHeaderView::Stretch);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_header);
    layout->addWidget(m_table);

    refresh();
}

void CursorPanel::setRecorder(const Recorder *recorder)
{
    m_recorder = recorder;
    refresh();
}

void CursorPanel::setTags(const QList<Tag> &tags)
{
    m_tags = tags;
    refresh();
}

void CursorPanel::setCursors(std::optional<double> t1, std::optional<double> t2)
{
    m_t1 = t1;
    m_t2 = t2;
    refresh();
}

QString CursorPanel::readoutText() const
{
    if (!m_t1 && !m_t2)
        return {};

    QStringList lines{headerLine(m_t1, m_t2)};
    for (const Tag &tag : m_tags) {
        if (!tag.plotted)
            continue;
        const RowValues v = valuesFor(m_recorder, tag, m_t1, m_t2);
        lines << v.label + QStringLiteral(": Y1 = ") + v.y1
                   + QStringLiteral(", Y2 = ") + v.y2
                   + QStringLiteral(", ") + kDelta + QStringLiteral("Y = ") + v.dy;
    }
    return lines.join(QLatin1Char('\n'));
}

void CursorPanel::refresh()
{
    m_header->setText(headerLine(m_t1, m_t2));

    QList<const Tag *> shown;
    for (const Tag &tag : m_tags) {
        if (tag.plotted)
            shown.append(&tag);
    }

    const int rows = static_cast<int>(shown.size());
    m_table->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        const Tag &tag = *shown.at(row);
        const RowValues v = valuesFor(m_recorder, tag, m_t1, m_t2);

        const QString texts[kColumnCount] = {v.label, v.y1, v.y2, v.dy};
        for (int col = 0; col < kColumnCount; ++col) {
            QTableWidgetItem *item = m_table->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (col != kColTag)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_table->setItem(row, col, item);
            }
            item->setText(texts[col]);
        }
        m_table->item(row, kColTag)->setData(Qt::DecorationRole, QColor(tag.color));
    }
}

} // namespace mb
