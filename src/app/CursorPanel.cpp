#include "CursorPanel.h"

#include "NumberText.h"
#include "PlotCursors.h"
#include "Recorder.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPalette>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace mb {

namespace {

const QString kDash = QStringLiteral(u"—");  // em dash: missing value
const QString kDelta = QStringLiteral(u"Δ"); // capital delta
const QString kArrow = QStringLiteral(u"→"); // right arrow

constexpr int kColTag = 0;

double nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

QString fmtNum(double v)
{
    return std::isfinite(v) ? numberText(v, 6) : kDash;
}

QString fmtTime(double t)
{
    return QString::number(t, 'f', 3) + QStringLiteral(" s");
}

QString cursorName(int n)
{
    return QStringLiteral("C") + QString::number(n);
}

QString tagLabel(const Tag &tag)
{
    return tag.unit.isEmpty() ? tag.name
                              : tag.name + QStringLiteral(" [") + tag.unit + QStringLiteral("]");
}

QString frequencyText(double dt)
{
    return dt != 0.0 ? numberText(1.0 / std::abs(dt), 4) + QStringLiteral(" Hz") : kDash;
}

} // namespace

CursorPanel::CursorPanel(QWidget *parent) : QWidget(parent)
{
    m_noCursors = new QLabel(tr("No cursors. Use Place cursors on the Trend to place them."), this);
    m_noCursors->setObjectName(QStringLiteral("noCursorsLabel"));
    m_noCursors->setWordWrap(true);

    m_list = new QWidget(this);
    m_list->setObjectName(QStringLiteral("cursorList"));
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(2);

    m_compare = new QWidget(this);
    m_compare->setObjectName(QStringLiteral("compareBox"));
    auto *compareLayout = new QVBoxLayout(m_compare);
    compareLayout->setContentsMargins(0, 0, 0, 0);
    auto *pairRow = new QHBoxLayout;
    m_fromCombo = new QComboBox(m_compare);
    m_fromCombo->setObjectName(QStringLiteral("compareFromCombo"));
    m_toCombo = new QComboBox(m_compare);
    m_toCombo->setObjectName(QStringLiteral("compareToCombo"));
    pairRow->addWidget(new QLabel(tr("Compare"), m_compare));
    pairRow->addWidget(m_fromCombo);
    pairRow->addWidget(new QLabel(kArrow, m_compare));
    pairRow->addWidget(m_toCombo);
    pairRow->addStretch(1);
    m_dtLabel = new QLabel(m_compare);
    m_dtLabel->setObjectName(QStringLiteral("deltaTLabel"));
    m_dtLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_freqLabel = new QLabel(m_compare);
    m_freqLabel->setObjectName(QStringLiteral("freqLabel"));
    m_freqLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    compareLayout->addLayout(pairRow);
    compareLayout->addWidget(m_dtLabel);
    compareLayout->addWidget(m_freqLabel);

    m_table = new QTableWidget(0, 1, this);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_noCursors);
    layout->addWidget(m_list);
    layout->addWidget(m_compare);
    layout->addWidget(m_table, 1);

    connect(m_fromCombo, &QComboBox::currentIndexChanged, this, [this] { onComboChanged(); });
    connect(m_toCombo, &QComboBox::currentIndexChanged, this, [this] { onComboChanged(); });

    apply({});
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

void CursorPanel::setCursors(const QMap<int, double> &cursors)
{
    QMap<int, double> valid;
    for (auto it = cursors.cbegin(); it != cursors.cend(); ++it) {
        if (it.key() >= 1 && it.key() <= PlotCursors::kMaxCursors)
            valid.insert(it.key(), it.value());
    }
    apply(valid);
}

void CursorPanel::setCursors(std::optional<double> t1, std::optional<double> t2)
{
    QMap<int, double> m;
    if (t1)
        m.insert(1, *t1);
    if (t2)
        m.insert(2, *t2);
    apply(m);
}

std::pair<int, int> CursorPanel::comparedPair() const
{
    return {m_from, m_to};
}

bool CursorPanel::pairComplete() const
{
    return m_cursors.contains(m_from) && m_cursors.contains(m_to);
}

void CursorPanel::apply(const QMap<int, double> &cursors)
{
    const bool sameSet = cursors.keys() == m_cursors.keys();
    m_cursors = cursors;
    updatePair();
    if (!sameSet || m_rows.size() != m_cursors.size())
        rebuildCursorRows();
    rebuildCompareCombos();
    refresh();
}

void CursorPanel::updatePair()
{
    // Keep the chosen pair while both cursors exist; otherwise (with at least two cursors)
    // fall back to the two lowest existing ones.
    if (pairComplete() || m_cursors.size() < 2)
        return;
    const QList<int> keys = m_cursors.keys();
    m_from = keys.at(0);
    m_to = keys.at(1);
}

void CursorPanel::rebuildCursorRows()
{
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        it->widget->hide();
        it->widget->setParent(nullptr); // a remove button of this row may be the sender
        it->widget->deleteLater();
    }
    m_rows.clear();

    for (auto it = m_cursors.cbegin(); it != m_cursors.cend(); ++it) {
        const int n = it.key();
        auto *row = new QWidget(m_list);
        row->setObjectName(QStringLiteral("cursorRow%1").arg(n));
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *name = new QLabel(cursorName(n), row);
        name->setObjectName(QStringLiteral("cursorName%1").arg(n));
        QPalette pal = name->palette();
        pal.setColor(QPalette::WindowText, PlotCursors::colorOf(n));
        name->setPalette(pal);
        QFont f = name->font();
        f.setBold(true);
        name->setFont(f);
        auto *time = new QLabel(row);
        time->setObjectName(QStringLiteral("cursorTime%1").arg(n));
        time->setTextInteractionFlags(Qt::TextSelectableByMouse);
        auto *remove = new QToolButton(row);
        remove->setObjectName(QStringLiteral("removeCursor%1").arg(n));
        remove->setText(QStringLiteral(u"×"));
        remove->setAutoRaise(true);
        remove->setToolTip(tr("Remove C%1").arg(n));
        connect(remove, &QToolButton::clicked, this, [this, n] { emit removeCursorRequested(n); });
        h->addWidget(name);
        h->addWidget(time, 1);
        h->addWidget(remove);
        m_listLayout->addWidget(row);
        m_rows.insert(n, CursorRow{row, time});
    }
}

void CursorPanel::rebuildCompareCombos()
{
    m_updating = true;
    for (QComboBox *combo : {m_fromCombo, m_toCombo}) {
        combo->clear();
        for (auto it = m_cursors.cbegin(); it != m_cursors.cend(); ++it)
            combo->addItem(cursorName(it.key()), it.key());
    }
    m_fromCombo->setCurrentIndex(m_fromCombo->findData(m_from)); // -1 when missing
    m_toCombo->setCurrentIndex(m_toCombo->findData(m_to));
    m_updating = false;
}

void CursorPanel::onComboChanged()
{
    if (m_updating)
        return;
    if (m_fromCombo->currentIndex() >= 0)
        m_from = m_fromCombo->currentData().toInt();
    if (m_toCombo->currentIndex() >= 0)
        m_to = m_toCombo->currentData().toInt();
    refresh();
}

QString CursorPanel::readoutText() const
{
    if (m_cursors.isEmpty())
        return {};

    QStringList head;
    for (auto it = m_cursors.cbegin(); it != m_cursors.cend(); ++it)
        head << cursorName(it.key()) + QStringLiteral(" = ") + fmtTime(it.value());
    QStringList lines{head.join(QStringLiteral("   "))};

    const bool pair = pairComplete();
    if (pair) {
        const double d = m_cursors.value(m_to) - m_cursors.value(m_from);
        lines << kDelta + QStringLiteral("t (") + cursorName(m_from) + kArrow + cursorName(m_to)
                     + QStringLiteral(") = ") + fmtTime(d) + QStringLiteral("   1/") + kDelta
                     + QStringLiteral("t = ") + frequencyText(d);
    }

    for (const Tag &tag : m_tags) {
        if (!tag.plotted)
            continue;
        QStringList parts;
        QMap<int, double> y;
        for (auto it = m_cursors.cbegin(); it != m_cursors.cend(); ++it) {
            const double v = m_recorder ? m_recorder->sampleAt(tag.id, it.value()) : nan();
            y.insert(it.key(), v);
            parts << cursorName(it.key()) + QLatin1Char(' ') + fmtNum(v);
        }
        if (pair)
            parts << kDelta + QStringLiteral("Y ") + fmtNum(y.value(m_to) - y.value(m_from));
        lines << tagLabel(tag) + QStringLiteral(": ") + parts.join(QStringLiteral("   "));
    }
    return lines.join(QLatin1Char('\n'));
}

void CursorPanel::refresh()
{
    const bool any = !m_cursors.isEmpty();
    m_noCursors->setVisible(!any);
    m_list->setVisible(any);
    m_compare->setVisible(any);

    for (auto it = m_rows.cbegin(); it != m_rows.cend(); ++it)
        it->time->setText(fmtTime(m_cursors.value(it.key())));

    const bool pair = pairComplete();
    QString dt = kDash;
    QString hz = kDash;
    if (pair) {
        const double d = m_cursors.value(m_to) - m_cursors.value(m_from);
        dt = fmtTime(d);
        hz = frequencyText(d);
    }
    m_dtLabel->setText(kDelta + QStringLiteral("t = ") + dt);
    m_freqLabel->setText(QStringLiteral("1/") + kDelta + QStringLiteral("t = ") + hz);

    // Columns: Tag, Y@Cn per cursor, then ΔY of the compared pair.
    const QList<int> keys = m_cursors.keys();
    const int colCount = 1 + int(keys.size()) + (pair ? 1 : 0);
    m_table->setColumnCount(colCount);
    auto setHeader = [this](int col, const QString &text, const QColor &color) {
        auto *item = new QTableWidgetItem(text);
        if (color.isValid())
            item->setForeground(color);
        m_table->setHorizontalHeaderItem(col, item);
    };
    setHeader(kColTag, tr("Tag"), QColor());
    for (int i = 0; i < keys.size(); ++i)
        setHeader(1 + i, QStringLiteral("Y@") + cursorName(keys.at(i)),
                  PlotCursors::colorOf(keys.at(i)));
    if (pair)
        setHeader(colCount - 1,
                  kDelta + QStringLiteral("Y ") + cursorName(m_from) + kArrow + cursorName(m_to),
                  QColor());

    QList<const Tag *> shown;
    for (const Tag &tag : m_tags) {
        if (tag.plotted)
            shown.append(&tag);
    }

    const int rows = static_cast<int>(shown.size());
    m_table->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        const Tag &tag = *shown.at(row);
        QStringList texts{tagLabel(tag)};
        QMap<int, double> y;
        for (int key : keys) {
            const double v = m_recorder ? m_recorder->sampleAt(tag.id, m_cursors.value(key)) : nan();
            y.insert(key, v);
            texts << fmtNum(v);
        }
        if (pair)
            texts << fmtNum(y.value(m_to) - y.value(m_from)); // NaN propagates

        for (int col = 0; col < colCount; ++col) {
            QTableWidgetItem *item = m_table->item(row, col);
            if (!item) {
                item = new QTableWidgetItem;
                item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                if (col != kColTag)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_table->setItem(row, col, item);
            }
            item->setText(texts.at(col));
        }
        m_table->item(row, kColTag)->setData(Qt::DecorationRole, QColor(tag.color));
    }
}

} // namespace mb
