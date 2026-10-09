#include "WriteController.h"

#include "TagCodec.h"
#include "TagTableModel.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>

#include <algorithm>

namespace mb {

namespace {

QString typeText(const Tag &tag)
{
    return toString(tag.type);
}

QString rawText(const Tag &tag, quint16 raw)
{
    return tag.type == ValueType::Int16 ? QString::number(static_cast<qint16>(raw))
                                        : QString::number(raw);
}

QString valueText(double v)
{
    return QString::number(v, 'g', 6);
}

QString tagLabel(const Tag &tag)
{
    return QStringLiteral("%1 (HR %2)").arg(tag.name).arg(tag.address);
}

/// One line edit per tag; values are parsed with the C locale.
class ValuesDialog : public QDialog
{
public:
    ValuesDialog(const QList<Tag> &tags, const QList<double> &current, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QObject::tr("Write registers"));
        auto *form = new QFormLayout(this);
        auto *validator = new QDoubleValidator(this);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::StandardNotation);
        for (qsizetype i = 0; i < tags.size(); ++i) {
            auto *edit = new QLineEdit(valueText(i < current.size() ? current.at(i) : 0.0), this);
            edit->setValidator(validator);
            const QString unit = tags.at(i).unit.isEmpty() ? QString()
                                                           : QStringLiteral(" [%1]").arg(tags.at(i).unit);
            form->addRow(tagLabel(tags.at(i)) + unit, edit);
            m_edits.append(edit);
        }
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    std::optional<QList<double>> values() const
    {
        QList<double> out;
        for (const QLineEdit *edit : m_edits) {
            bool ok = false;
            const double v = QLocale::c().toDouble(edit->text().trimmed(), &ok);
            if (!ok)
                return std::nullopt;
            out.append(v);
        }
        return out;
    }

private:
    QList<QLineEdit *> m_edits;
};

} // namespace

WriteController::WriteController(TagTableModel *model, QWidget *dialogParent, QObject *parent)
    : QObject(parent), m_model(model), m_dialogParent(dialogParent)
{
    m_confirm = [this](const QString &question) {
        return QMessageBox::question(m_dialogParent, tr("Confirm write"), question,
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               == QMessageBox::Yes;
    };
    m_warn = [this](const QString &text) {
        QMessageBox::warning(m_dialogParent, tr("Write"), text);
    };
    m_askValues = [this](const QList<Tag> &tags,
                         const QList<double> &current) -> std::optional<QList<double>> {
        ValuesDialog dlg(tags, current, m_dialogParent);
        while (dlg.exec() == QDialog::Accepted) {
            if (auto v = dlg.values())
                return v;
            m_warn(tr("Please enter a valid number in every field."));
        }
        return std::nullopt;
    };
}

void WriteController::requestWrite(TagId id, double scaledValue)
{
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    const int row = m_model->rowOf(id);
    if (row < 0)
        return;
    const Tag tag = m_model->tagAt(row);
    if (!tag.isWritable())
        return;

    const auto raw = encodeValue(tag, scaledValue);
    if (!raw) {
        m_warn(tr("Value %1 is out of range for %2 (%3, gain %4, offset %5).")
                   .arg(valueText(scaledValue), tag.name, typeText(tag), valueText(tag.gain),
                        valueText(tag.offset)));
        return;
    }

    const QString unit = tag.unit.isEmpty() ? QString() : QStringLiteral(" ") + tag.unit;
    if (!m_confirm(tr("Write %1%2 (raw %3) to HR %4 '%5'?")
                       .arg(valueText(scaledValue), unit, rawText(tag, *raw))
                       .arg(tag.address)
                       .arg(tag.name)))
        return;

    // The connection may have dropped while the dialog was open.
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    const quint64 reqId = m_nextRequest++;
    m_pending.insert(reqId, {id});
    emit message(tr("Writing %1 to HR %2 '%3' ...").arg(valueText(scaledValue)).arg(tag.address).arg(tag.name));
    emit writeRegisters(reqId, tag.address, {*raw});
}

void WriteController::requestWriteSelection(const QList<int> &rows)
{
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }

    QList<Tag> tags;
    for (int row : rows) {
        if (row < 0 || row >= m_model->rowCount())
            continue;
        const Tag &tag = m_model->tagAt(row);
        if (tags.contains(tag))
            continue;
        if (!tag.isWritable()) {
            m_warn(tr("'%1' is not a writable holding register.").arg(tag.name));
            return;
        }
        tags.append(tag);
    }
    if (tags.size() < 2 || tags.size() > 123) {
        m_warn(tr("Select between 2 and 123 writable holding registers (selected: %1).").arg(tags.size()));
        return;
    }
    std::sort(tags.begin(), tags.end(), [](const Tag &a, const Tag &b) { return a.address < b.address; });
    for (qsizetype i = 1; i < tags.size(); ++i) {
        if (tags.at(i).address == tags.at(i - 1).address) {
            m_warn(tr("'%1' and '%2' use the same address (HR %3).")
                       .arg(tags.at(i - 1).name, tags.at(i).name)
                       .arg(tags.at(i).address));
            return;
        }
        if (tags.at(i).address != tags.at(i - 1).address + 1) {
            m_warn(tr("The selected registers are not consecutive: gap between HR %1 and HR %2.")
                       .arg(tags.at(i - 1).address)
                       .arg(tags.at(i).address));
            return;
        }
    }

    // Prefill with the current scaled values (Value column), else decode of 0.
    QList<double> current;
    for (const Tag &tag : tags) {
        double v = decodeValue(tag, 0);
        const int row = m_model->rowOf(tag.id);
        if (row >= 0) {
            const QVariant text = m_model->data(m_model->index(row, TagTableModel::Value));
            bool ok = false;
            const double parsed = text.toString().toDouble(&ok);
            if (ok)
                v = parsed;
        }
        current.append(v);
    }

    const auto values = m_askValues(tags, current);
    if (!values || values->size() != tags.size())
        return;

    QList<quint16> raws;
    QStringList bad;
    for (qsizetype i = 0; i < tags.size(); ++i) {
        const auto raw = encodeValue(tags.at(i), values->at(i));
        if (!raw)
            bad.append(tr("%1 = %2 (%3, gain %4, offset %5)")
                           .arg(tags.at(i).name, valueText(values->at(i)), typeText(tags.at(i)),
                                valueText(tags.at(i).gain), valueText(tags.at(i).offset)));
        else
            raws.append(*raw);
    }
    if (!bad.isEmpty()) {
        m_warn(tr("Out of range values:\n%1").arg(bad.join(QLatin1Char('\n'))));
        return;
    }

    const quint16 first = tags.first().address;
    const quint16 last = tags.last().address;
    if (!m_confirm(tr("Write %1 registers HR %2..%3?").arg(tags.size()).arg(first).arg(last)))
        return;

    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    QList<TagId> ids;
    for (const Tag &tag : tags)
        ids.append(tag.id);
    const quint64 reqId = m_nextRequest++;
    m_pending.insert(reqId, ids);
    emit message(tr("Writing %1 registers HR %2..%3 ...").arg(tags.size()).arg(first).arg(last));
    emit writeRegisters(reqId, first, raws);
}

void WriteController::onWriteFinished(quint64 requestId, const ModbusResult &result)
{
    const auto it = m_pending.find(requestId);
    if (it == m_pending.end())
        return;
    const QList<TagId> ids = it.value();
    m_pending.erase(it);

    QStringList names;
    for (TagId id : ids) {
        const int row = m_model->rowOf(id);
        if (row >= 0)
            names.append(m_model->tagAt(row).name);
    }

    if (result.ok()) {
        for (TagId id : ids)
            m_model->setStatusText(id, QString());
        emit message(tr("Write OK: %1").arg(names.join(QStringLiteral(", "))));
    } else {
        const QString text = tr("Write failed: %1").arg(result.message);
        for (TagId id : ids)
            m_model->setStatusText(id, text);
        emit message(text);
    }
}

void WriteController::setConnected(bool connected)
{
    m_connected = connected;
    if (!connected)
        m_pending.clear();
}

} // namespace mb
