#include "ConnectionBar.h"

// Stub: implemented by its task (see TASKS.md).

namespace mb {

ConnectionBar::ConnectionBar(QWidget *parent) : QWidget(parent) {}
ConnectionSettings ConnectionBar::settings() const { return {}; }
void ConnectionBar::setSettings(const ConnectionSettings &) {}
bool ConnectionBar::hasValidInput() const { return true; }
void ConnectionBar::setState(ConnectionState state, const QString &) { m_state = state; }

} // namespace mb