#include "QuickWritePanel.h"

#include "TagTableModel.h"

// Stub created by T100; implemented by T107.

namespace mb {

QuickWritePanel::QuickWritePanel(TagTableModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
}

void QuickWritePanel::setConnected(bool connected) { m_connected = connected; }

} // namespace mb
