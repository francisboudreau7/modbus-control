#include "SimulationDialog.h"

#include <QListWidget>

namespace mb {

// Stub until the dialog task.
SimulationDialog::SimulationDialog(const QList<SimulationEntry> &entries, QWidget *parent)
    : QDialog(parent), m_entries(entries)
{
}

int SimulationDialog::selectedIndex() const
{
    return -1;
}

} // namespace mb
