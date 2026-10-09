#include "ConnectionBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QValidator>

namespace mb {

namespace {

// Matches an IPv4 dotted quad (each octet 0..255) or exactly "localhost".
const QString kHostPattern = QStringLiteral(
    R"(^(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$|^localhost$)");

QString stateColor(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Disconnected: return QStringLiteral("#9e9e9e");
    case ConnectionState::Connecting:   return QStringLiteral("#f2c200");
    case ConnectionState::Connected:    return QStringLiteral("#2e9e44");
    case ConnectionState::Reconnecting: return QStringLiteral("#f08c00");
    }
    return QStringLiteral("#9e9e9e");
}

} // namespace

ConnectionBar::ConnectionBar(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);

    m_led = new QLabel(this);
    m_led->setFixedSize(12, 12);
    layout->addWidget(m_led);

    layout->addWidget(new QLabel(QStringLiteral("Host"), this));
    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(QStringLiteral("127.0.0.1"));
    m_host->setToolTip(QStringLiteral("IPv4 address or localhost"));
    m_host->setValidator(new QRegularExpressionValidator(QRegularExpression(kHostPattern), m_host));
    m_host->setFixedWidth(120);
    layout->addWidget(m_host);

    auto makeSpin = [this, layout](const QString &label, int min, int max, const QString &suffix,
                                   int width) {
        layout->addWidget(new QLabel(label, this));
        auto *spin = new QSpinBox(this);
        spin->setRange(min, max);
        if (!suffix.isEmpty())
            spin->setSuffix(suffix);
        spin->setFixedWidth(width);
        layout->addWidget(spin);
        return spin;
    };

    m_port = makeSpin(QStringLiteral("Port"), 1, 65535, QString(), 70);
    m_unit = makeSpin(QStringLiteral("Unit"), 0, 255, QString(), 60);
    m_timeout = makeSpin(QStringLiteral("Timeout"), 100, 60000, QStringLiteral(" ms"), 90);
    m_period = makeSpin(QStringLiteral("Period"), 100, 60000, QStringLiteral(" ms"), 90);

    m_button = new QPushButton(this);
    m_button->setFixedWidth(100);
    layout->addWidget(m_button);
    layout->addStretch(1);

    connect(m_host, &QLineEdit::textChanged, this, [this] { updateButton(); });
    connect(m_host, &QLineEdit::textEdited, this, [this] { emit settingsEdited(); });
    connect(m_port, &QSpinBox::valueChanged, this, [this] { emit settingsEdited(); });
    connect(m_unit, &QSpinBox::valueChanged, this, [this] { emit settingsEdited(); });
    connect(m_timeout, &QSpinBox::valueChanged, this, [this] { emit settingsEdited(); });
    connect(m_period, &QSpinBox::valueChanged, this, [this] { emit settingsEdited(); });

    connect(m_host, &QLineEdit::returnPressed, this, [this] {
        if (m_state == ConnectionState::Disconnected && hasValidInput())
            emit connectRequested(settings());
    });
    connect(m_button, &QPushButton::clicked, this, [this] {
        if (m_state == ConnectionState::Disconnected)
            emit connectRequested(settings());
        else
            emit disconnectRequested();
    });

    setSettings(ConnectionSettings{});
    setState(ConnectionState::Disconnected, QString());
}

ConnectionSettings ConnectionBar::settings() const
{
    ConnectionSettings s;
    s.host = m_host->text();
    s.port = static_cast<quint16>(m_port->value());
    s.unitId = m_unit->value();
    s.timeoutMs = m_timeout->value();
    s.pollPeriodMs = m_period->value();
    return s;
}

void ConnectionBar::setSettings(const ConnectionSettings &settings)
{
    {
        QSignalBlocker b1(m_host);
        QSignalBlocker b2(m_port);
        QSignalBlocker b3(m_unit);
        QSignalBlocker b4(m_timeout);
        QSignalBlocker b5(m_period);
        m_host->setText(settings.host);
        m_port->setValue(settings.port);
        m_unit->setValue(settings.unitId);
        m_timeout->setValue(settings.timeoutMs);
        m_period->setValue(settings.pollPeriodMs);
    }
    updateButton();
}

bool ConnectionBar::hasValidInput() const
{
    QString text = m_host->text();
    int pos = 0;
    const QValidator *validator = m_host->validator();
    return validator && validator->validate(text, pos) == QValidator::Acceptable;
}

void ConnectionBar::setState(ConnectionState state, const QString &detail)
{
    m_state = state;

    const bool editable = state == ConnectionState::Disconnected;
    m_host->setEnabled(editable);
    m_port->setEnabled(editable);
    m_unit->setEnabled(editable);
    m_timeout->setEnabled(editable);
    m_period->setEnabled(editable);

    m_led->setStyleSheet(QStringLiteral("border-radius:6px; background:%1;").arg(stateColor(state)));
    m_led->setToolTip(detail.isEmpty() ? toString(state) : detail);

    updateButton();
}

void ConnectionBar::updateButton()
{
    if (m_state == ConnectionState::Disconnected) {
        m_button->setText(QStringLiteral("Connect"));
        m_button->setEnabled(hasValidInput());
    } else {
        m_button->setText(QStringLiteral("Disconnect"));
        m_button->setEnabled(true);
    }
}

} // namespace mb
