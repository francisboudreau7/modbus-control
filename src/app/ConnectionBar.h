#pragma once

#include "ModbusConnection.h"
#include "PollWorker.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace mb {

/// Toolbar widget with the connection fields and the Connect/Disconnect button.
/// Layout (one row): status LED, Host [line edit], Port [1..65535], Unit [0..247],
/// Timeout ms [100..60000], Period ms [100..60000], "FC06 only" checkbox
/// (singleRegisterWrites; tooltip: write multi-register values as one FC06 per register,
/// for devices without FC16), Connect button.
/// Pure view: it never talks to the PollWorker itself.
class ConnectionBar : public QWidget
{
    Q_OBJECT
public:
    explicit ConnectionBar(QWidget *parent = nullptr);

    /// Current field values.
    ConnectionSettings settings() const;
    /// Fills the fields; does not emit settingsEdited().
    void setSettings(const ConnectionSettings &settings);
    /// False when the host field is empty (Connect is disabled then).
    bool hasValidInput() const;

public slots:
    /// Disconnected: fields editable, button "Connect", grey LED.
    /// Connecting: fields read-only, button "Disconnect", yellow LED.
    /// Connected: fields read-only, button "Disconnect", green LED.
    /// Reconnecting: fields read-only, button "Disconnect", orange LED.
    /// `detail` becomes the LED tooltip.
    void setState(mb::ConnectionState state, const QString &detail);

signals:
    /// Button pressed while Disconnected.
    void connectRequested(const mb::ConnectionSettings &settings);
    /// Button pressed in any other state.
    void disconnectRequested();
    /// The user changed any field (used for the project's modified flag).
    void settingsEdited();

private:
    // Implementation-defined; the task may change the private section freely.
    void updateButton();

    ConnectionState m_state = ConnectionState::Disconnected;
    QLabel *m_led = nullptr;
    QLineEdit *m_host = nullptr;
    QSpinBox *m_port = nullptr;
    QSpinBox *m_unit = nullptr;
    QSpinBox *m_timeout = nullptr;
    QSpinBox *m_period = nullptr;
    QCheckBox *m_singleWrites = nullptr;
    QPushButton *m_button = nullptr;
};

} // namespace mb
