#include "EncodingDialog.h"

// Stub created by T150; implemented by T152.

namespace mb {

EncodingDialog::EncodingDialog(const Tag &tag, const QList<RegisterPair> &readings,
                               const QList<Tag> &others, std::optional<ByteOrder> deviceOrder,
                               int addressBase, QWidget *parent)
    : QDialog(parent), m_tag(tag), m_readings(readings), m_others(others),
      m_deviceOrder(deviceOrder), m_addressBase(addressBase)
{
}

void EncodingDialog::addReading(const RegisterPair &reading)
{
    m_readings.append(reading);
}

void EncodingDialog::setDisplayValue(const QString &) {}

EncodingResult EncodingDialog::result() const
{
    return m_result;
}

std::optional<Encoding> EncodingDialog::chosenEncoding() const
{
    return std::nullopt;
}

bool EncodingDialog::matchedDisplayValue() const
{
    return false;
}

QList<TagId> EncodingDialog::applyOrderTo() const
{
    return {};
}

void EncodingDialog::rerank() {}

} // namespace mb
