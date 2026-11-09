#pragma once

#include "SimulationCatalog.h"

#include <QDialog>
#include <QList>

class QLabel;
class QListWidget;

namespace mb {

/// Simulation > Load simulation... (v1.3.4): the supported simulations in a list, no file dialog.
/// Title "Load simulation". Children: QListWidget "simulationList" (the names, first entry
/// selected), QLabel "simulationDescription" (word-wrapped, the selected entry's description),
/// a button box with "Load" (accepts; its objectName is "simulationLoad") and Cancel. A
/// double-click on an entry accepts. An empty list disables Load.
class SimulationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SimulationDialog(const QList<SimulationEntry> &entries, QWidget *parent = nullptr);

    /// Index in `entries` of the selected entry, -1 when there is none.
    int selectedIndex() const;

private:
    // Implementation-defined; the task may change the private section freely.
    QList<SimulationEntry> m_entries;
    QListWidget *m_list = nullptr;
    QLabel *m_description = nullptr;
};

} // namespace mb
