#include "EncodingMonitor.h"

#include "EncodingDialog.h"
#include "TagTableModel.h"

// Stub created by T150; implemented by T153.

namespace mb {

EncodingMonitor::EncodingMonitor(TagTableModel *model, QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_model(model), m_dialogParent(dialogParent)
{
    m_runDialog = [](EncodingDialog &dialog) { return dialog.exec() == QDialog::Accepted; };
    m_reviewPrompt = [](const QString &) { return false; };
}

std::optional<ByteOrder> EncodingMonitor::deviceOrder() const
{
    return std::nullopt;
}

bool EncodingMonitor::isPending(TagId id) const
{
    return m_pending.contains(id);
}

void EncodingMonitor::onSamples(const SampleBatch &) {}

void EncodingMonitor::onConnected()
{
    m_connected = true;
}

void EncodingMonitor::onDisconnected()
{
    m_connected = false;
    m_readings.clear();
}

void EncodingMonitor::onTagsChanged() {}

void EncodingMonitor::onTagsReplaced() {}

void EncodingMonitor::detectNow(TagId) {}

} // namespace mb
