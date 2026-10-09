#include "WriteController.h"

// Stub: implemented by its task (see TASKS.md).

namespace mb {

WriteController::WriteController(TagTableModel *model, QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_model(model), m_dialogParent(dialogParent)
{
}
void WriteController::requestWrite(TagId, double) {}
void WriteController::requestWriteSelection(const QList<int> &) {}
void WriteController::onWriteFinished(quint64, const ModbusResult &) {}
void WriteController::setConnected(bool connected) { m_connected = connected; }

} // namespace mb