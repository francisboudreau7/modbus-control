#include "QuickWritePanel.h"

#include "TagTableModel.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace mb {

namespace {

QString buttonText(const QString &prefix, double value, const QString &unit)
{
    QString text = QStringLiteral("%1: %2").arg(prefix, QString::number(value, 'g', 6));
    if (!unit.isEmpty())
        text += QLatin1Char(' ') + unit;
    return text;
}

} // namespace

QuickWritePanel::QuickWritePanel(TagTableModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    m_content = new QWidget(this);
    outer->addWidget(m_content);
    outer->addStretch(1);

    connect(m_model, &QAbstractItemModel::modelReset, this, &QuickWritePanel::rebuild);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &QuickWritePanel::rebuild);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &QuickWritePanel::rebuild);
    connect(m_model, &TagTableModel::tagsEdited, this, &QuickWritePanel::rebuild);
    connect(m_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                if (topLeft.column() > TagTableModel::Value || bottomRight.column() < TagTableModel::Value)
                    return;
                for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
                    updateValue(row);
            });
    rebuild();
}

void QuickWritePanel::setConnected(bool connected)
{
    m_connected = connected;
    const auto buttons = m_content->findChildren<QPushButton *>();
    for (QPushButton *button : buttons)
        button->setEnabled(m_connected);
}

QString QuickWritePanel::valueText(int row) const
{
    const QString text = m_model->data(m_model->index(row, TagTableModel::Value)).toString();
    return text.isEmpty() ? QString(QChar(0x2014)) : text;
}

void QuickWritePanel::updateValue(int row)
{
    if (row < 0 || row >= m_model->rowCount())
        return;
    const auto it = m_valueLabels.constFind(m_model->tagAt(row).id);
    if (it != m_valueLabels.constEnd())
        it.value()->setText(valueText(row));
}

void QuickWritePanel::rebuild()
{
    // Replace the whole content widget; the old one goes away immediately.
    m_valueLabels.clear();
    delete m_content;
    m_content = new QWidget(this);
    static_cast<QVBoxLayout *>(layout())->insertWidget(0, m_content);

    auto *grid = new QGridLayout(m_content);
    grid->setContentsMargins(0, 0, 0, 0);
    int gridRow = 0;
    const int count = m_model->rowCount();
    for (int row = 0; row < count; ++row) {
        const Tag &tag = m_model->tagAt(row);
        if (!tag.isWritable() || (!tag.quickA && !tag.quickB))
            continue;
        const QString name = tag.unit.isEmpty() ? tag.name
                                                : QStringLiteral("%1 [%2]").arg(tag.name, tag.unit);
        grid->addWidget(new QLabel(name, m_content), gridRow, 0);
        auto *valueLabel = new QLabel(valueText(row), m_content);
        valueLabel->setObjectName(QStringLiteral("quickValue_%1").arg(tag.id));
        grid->addWidget(valueLabel, gridRow, 1);
        m_valueLabels.insert(tag.id, valueLabel);

        const TagId id = tag.id;
        int col = 2;
        const std::pair<const char *, std::optional<double>> presets[] = {{"A", tag.quickA},
                                                                          {"B", tag.quickB}};
        for (const auto &[letter, preset] : presets) {
            if (!preset)
                continue;
            const double value = *preset;
            auto *button = new QPushButton(buttonText(QLatin1String(letter), value, tag.unit), m_content);
            button->setObjectName(QStringLiteral("quick%1_%2").arg(QLatin1String(letter)).arg(id));
            button->setEnabled(m_connected);
            connect(button, &QPushButton::clicked, this,
                    [this, id, value] { emit quickWriteRequested(id, value); });
            grid->addWidget(button, gridRow, col++);
        }
        ++gridRow;
    }
    if (gridRow == 0) {
        auto *hint = new QLabel(tr("Set Quick A / Quick B on writable tags in the tag table."), m_content);
        hint->setObjectName(QStringLiteral("quickHint"));
        hint->setWordWrap(true);
        grid->addWidget(hint, 0, 0);
    }
    grid->setColumnStretch(0, 1);
}

} // namespace mb
