#include "WriteController.h"

#include "TagCodec.h"
#include "TagTableModel.h"

#include <QCheckBox>
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

/// Raw value as shown in the confirmation: decimal for 16-bit, 0x%08X (ABCD pattern) for 32-bit.
QString rawText(const Tag &tag, const QList<quint16> &regs)
{
    if (registerCount(tag.type) == 2 && regs.size() >= 2)
        return QStringLiteral("0x")
               + QString::number(combineRegisters(tag, regs.at(0), regs.at(1)), 16)
                     .rightJustified(8, QLatin1Char('0'))
                     .toUpper();
    const quint16 raw = regs.value(0);
    return tag.type == ValueType::Int16 ? QString::number(static_cast<qint16>(raw))
                                        : QString::number(raw);
}

QString registerRange(const Tag &tag, int base)
{
    const int n = registerCount(tag.type);
    return n == 2 ? QStringLiteral("HR %1-%2").arg(tag.address + base).arg(tag.address + 1 + base)
                  : QStringLiteral("HR %1").arg(tag.address + base);
}

QString valueText(double v)
{
    return QString::number(v, 'g', 6);
}

QString tagLabel(const Tag &tag, int base)
{
    return QStringLiteral("%1 (HR %2)").arg(tag.name).arg(tag.address + base);
}

/// One line edit per tag; values are parsed with the C locale.
class ValuesDialog : public QDialog
{
public:
    ValuesDialog(const QList<Tag> &tags, const QList<double> &current, int base, QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(QObject::tr("Write registers"));
        auto *form = new QFormLayout(this);
        auto *validator = new QDoubleValidator(this);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::StandardNotation);
        for (qsizetype i = 0; i < tags.size(); ++i) {
            // Shortest round-trip form, so an untouched field writes back exactly the same value.
            const double v = i < current.size() ? current.at(i) : 0.0;
            auto *edit = new QLineEdit(QString::number(v, 'g', QLocale::FloatingPointShortest), this);
            edit->setValidator(validator);
            const QString unit = tags.at(i).unit.isEmpty() ? QString()
                                                           : QStringLiteral(" [%1]").arg(tags.at(i).unit);
            form->addRow(tagLabel(tags.at(i), base) + unit, edit);
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
    m_quickConfirmPrompt = [this](const QString &question) {
        QMessageBox box(QMessageBox::Question, tr("Confirm write"), question,
                        QMessageBox::Yes | QMessageBox::No, m_dialogParent);
        box.setDefaultButton(QMessageBox::No);
        QCheckBox dontAsk(tr("Don't ask again this session"));
        box.setCheckBox(&dontAsk);
        const bool yes = box.exec() == QMessageBox::Yes;
        return QuickAnswer{yes, yes && dontAsk.isChecked()};
    };
    m_warn = [this](const QString &text) {
        QMessageBox::warning(m_dialogParent, tr("Write"), text);
    };
    m_askValues = [this](const QList<Tag> &tags,
                         const QList<double> &current) -> std::optional<QList<double>> {
        ValuesDialog dlg(tags, current, m_model->addressBase(), m_dialogParent);
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
    writeValue(id, scaledValue, false);
}

void WriteController::writeValue(TagId id, double scaledValue, bool quick)
{
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    const int row = m_model->rowOf(id);
    if (row < 0)
        return;
    const Tag tag = m_model->tagAt(row);
    const int base = m_model->addressBase();
    if (!tag.isWritable())
        return;

    const auto raw = encodeRegisters(tag, scaledValue);
    if (!raw) {
        m_warn(tr("Value %1 is out of range for %2 (%3, gain %4, offset %5).")
                   .arg(valueText(scaledValue), tag.name, typeText(tag), valueText(tag.gain),
                        valueText(tag.offset)));
        return;
    }

    const QString unit = tag.unit.isEmpty() ? QString() : QStringLiteral(" ") + tag.unit;
    const QString typeInfo = registerCount(tag.type) == 2
        ? tr(" [%1, %2]").arg(typeText(tag), toString(tag.byteOrder))
        : QString();
    const QString question = tr("Write %1%2 (raw %3) to %4 '%5'%6?")
                                 .arg(valueText(scaledValue), unit, rawText(tag, *raw),
                                      registerRange(tag, base), tag.name, typeInfo);
    if (quick) {
        if (m_quickConfirm) {
            const QuickAnswer answer = m_quickConfirmPrompt(question);
            if (!answer.confirmed)
                return;
            if (answer.dontAskAgain)
                setQuickConfirm(false);
        }
    } else if (!m_confirm(question)) {
        return;
    }

    // The connection may have dropped while the dialog was open.
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }
    const quint64 reqId = m_nextRequest++;
    m_pending.insert(reqId, {id});
    emit message(tr("Writing %1 to %2 '%3' ...").arg(valueText(scaledValue), registerRange(tag, base), tag.name));
    emit writeRegisters(reqId, tag.address, *raw);
}

void WriteController::requestWriteSelection(const QList<int> &rows)
{
    if (!m_connected) {
        emit message(tr("Not connected — cannot write."));
        return;
    }

    const int base = m_model->addressBase();
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
    int totalRegisters = 0;
    for (qsizetype i = 0; i < tags.size(); ++i) {
        totalRegisters += registerCount(tags.at(i).type);
        if (i == 0)
            continue;
        const Tag &prev = tags.at(i - 1);
        const Tag &next = tags.at(i);
        const int expected = prev.address + registerCount(prev.type);
        if (next.address < expected) {
            m_warn(tr("'%1' (%2) overlaps '%3' (%4).")
                       .arg(next.name, registerRange(next, base), prev.name, registerRange(prev, base)));
            return;
        }
        if (next.address != expected) {
            m_warn(tr("The selected registers are not consecutive: '%1' (%2) should start at HR %3 "
                      "to follow '%4' (%5), but starts at HR %6.")
                       .arg(next.name, registerRange(next, base))
                       .arg(expected + base)
                       .arg(prev.name, registerRange(prev, base))
                       .arg(next.address + base));
            return;
        }
    }
    if (totalRegisters > 123) {
        m_warn(tr("Too many registers: the selection spans %1 registers (maximum 123).").arg(totalRegisters));
        return;
    }

    // Prefill with the current scaled values (Value column), else decode of 0.
    QList<double> current;
    for (const Tag &tag : tags) {
        double v = decodeValue(tag, 0);
        const int row = m_model->rowOf(tag.id);
        if (row >= 0) {
            // EditRole carries the full-precision value; the display text is rounded.
            const QVariant live = m_model->data(m_model->index(row, TagTableModel::Value), Qt::EditRole);
            if (live.isValid())
                v = live.toDouble();
        }
        current.append(v);
    }

    const auto values = m_askValues(tags, current);
    if (!values || values->size() != tags.size())
        return;

    QList<quint16> raws;
    QStringList bad;
    for (qsizetype i = 0; i < tags.size(); ++i) {
        const auto raw = encodeRegisters(tags.at(i), values->at(i));
        if (!raw)
            bad.append(tr("%1 = %2 (%3, gain %4, offset %5)")
                           .arg(tags.at(i).name, valueText(values->at(i)), typeText(tags.at(i)),
                                valueText(tags.at(i).gain), valueText(tags.at(i).offset)));
        else
            raws += *raw;
    }
    if (!bad.isEmpty()) {
        m_warn(tr("Out of range values:\n%1").arg(bad.join(QLatin1Char('\n'))));
        return;
    }

    const quint16 first = tags.first().address;
    const int last = tags.last().address + registerCount(tags.last().type) - 1;
    const int shownFirst = first + base;
    const int shownLast = last + base;
    if (!m_confirm(tr("Write %1 registers HR %2..%3?").arg(totalRegisters).arg(shownFirst).arg(shownLast)))
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
    emit message(tr("Writing %1 registers HR %2..%3 ...").arg(totalRegisters).arg(shownFirst).arg(shownLast));
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

void WriteController::requestQuickWrite(mb::TagId id, double scaledValue)
{
    writeValue(id, scaledValue, true);
}

void WriteController::setQuickConfirm(bool on)
{
    if (m_quickConfirm == on)
        return;
    m_quickConfirm = on;
    emit quickConfirmChanged(on);
}

bool WriteController::quickConfirm() const { return m_quickConfirm; }

} // namespace mb
