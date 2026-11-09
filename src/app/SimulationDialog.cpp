#include "SimulationDialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace mb {

SimulationDialog::SimulationDialog(const QList<SimulationEntry> &entries, QWidget *parent)
    : QDialog(parent), m_entries(entries)
{
    setWindowTitle(tr("Load simulation"));

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("simulationList"));
    for (const SimulationEntry &entry : m_entries)
        m_list->addItem(entry.name);

    m_description = new QLabel(this);
    m_description->setObjectName(QStringLiteral("simulationDescription"));
    m_description->setWordWrap(true);
    m_description->setMinimumHeight(fontMetrics().lineSpacing() * 3);
    m_description->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton *load = buttons->button(QDialogButtonBox::Ok);
    load->setText(tr("Load"));
    load->setObjectName(QStringLiteral("simulationLoad"));
    load->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addWidget(m_description);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this, [this, load](int row) {
        m_description->setText(row >= 0 && row < m_entries.size() ? m_entries.at(row).description
                                                                  : QString());
        load->setEnabled(row >= 0);
    });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] {
        if (selectedIndex() >= 0)
            accept();
    });

    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    else
        load->setEnabled(false);
    resize(420, sizeHint().height());
}

int SimulationDialog::selectedIndex() const
{
    const int row = m_list->currentRow();
    return row >= 0 && row < m_entries.size() ? row : -1;
}

} // namespace mb
