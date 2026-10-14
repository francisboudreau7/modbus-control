#include "AddTagDialog.h"

#include "EncodingDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

// Stub created by T180; implemented by T183.

namespace mb {

AddTagDialog::AddTagDialog(const QList<Tag> &existing, int addressBase,
                           std::optional<ByteOrder> deviceOrder, QWidget *parent)
    : QDialog(parent), m_existing(existing), m_addressBase(addressBase), m_deviceOrder(deviceOrder)
{
    setWindowTitle(tr("Add tag"));
    m_runEncodingDialog = [](EncodingDialog &dialog) { return dialog.exec() == QDialog::Accepted; };
    auto *layout = new QVBoxLayout(this);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons);
}

AddTagDialog::~AddTagDialog() = default;

void AddTagDialog::setReadFunction(ReadFn read)
{
    m_read = std::move(read);
}

void AddTagDialog::setConnected(bool connected)
{
    m_connected = connected;
}

Tag AddTagDialog::tag() const
{
    return Tag();
}

bool AddTagDialog::detectionConfirmed() const
{
    return m_confirmed;
}

} // namespace mb
