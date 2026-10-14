#include "AddTagDialog.h"

#include "EncodingDialog.h"
#include "RichText.h"
#include "TagCodec.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QTimer>
#include <QVBoxLayout>

#include <deque>

namespace mb {

namespace {

constexpr int kAutoReadMs = 400;

QString hex16(quint16 v)
{
    return QStringLiteral("0x") + QStringLiteral("%1").arg(v, 4, 16, QLatin1Char('0')).toUpper();
}

QString number(double v)
{
    return QString::number(v, 'g', 7);
}

double decodeFloat(ByteOrder order, const RegisterPair &pair)
{
    Tag t;
    t.type = ValueType::Float32;
    t.byteOrder = order;
    return decodeValue(t, combineRegisters(t, pair.r0, pair.r1));
}

} // namespace

/// One probe in flight (shared with the read callbacks).
struct ProbeState {
    enum class Kind { Own, Below, Above, PairAt, PairBelow };
    struct Pending {
        Kind kind = Kind::Own;
        int start = 0;
        int count = 1;
    };

    Area area = Area::HoldingRegister;
    quint16 address = 0;
    quint64 generation = 0;
    bool forDetect = false;
    ProbeReadings readings;
    ModbusResult ownResult;
    QList<int> unreadNeighbours; ///< neighbour addresses whose read failed
    std::deque<Pending> queue;
    Pending current;
};

AddTagDialog::AddTagDialog(const QList<Tag> &existing, int addressBase,
                           std::optional<ByteOrder> deviceOrder, QWidget *parent)
    : QDialog(parent), m_existing(existing), m_addressBase(addressBase), m_deviceOrder(deviceOrder)
{
    setWindowTitle(tr("Add tag"));
    m_runEncodingDialog = [](EncodingDialog &dialog) { return dialog.exec() == QDialog::Accepted; };

    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("nameEdit"));
    m_name->setPlaceholderText(tr("Automatic"));

    m_area = new QComboBox(this);
    m_area->setObjectName(QStringLiteral("areaCombo"));
    m_area->addItem(tr("Holding register"));
    m_area->addItem(tr("Input register"));
    m_area->setCurrentIndex(-1);

    m_address = new QLineEdit(this);
    m_address->setObjectName(QStringLiteral("addressEdit"));
    m_address->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9]{0,6}")), m_address));
    m_address->setPlaceholderText(tr("%1..%2").arg(m_addressBase).arg(65535 + m_addressBase));

    m_type = new QComboBox(this);
    m_type->setObjectName(QStringLiteral("typeCombo"));
    for (ValueType t : {ValueType::Int16, ValueType::UInt16, ValueType::Int32, ValueType::UInt32,
                        ValueType::Float32})
        m_type->addItem(toString(t), int(t));

    m_order = new QComboBox(this);
    m_order->setObjectName(QStringLiteral("orderCombo"));
    for (ByteOrder o : {ByteOrder::ABCD, ByteOrder::CDAB, ByteOrder::BADC, ByteOrder::DCBA})
        m_order->addItem(toString(o), int(o));
    m_order->setEnabled(false);

    m_unit = new QLineEdit(this);
    m_unit->setObjectName(QStringLiteral("unitEdit"));

    m_writable = new QCheckBox(tr("Writable"), this);
    m_writable->setObjectName(QStringLiteral("writableCheck"));
    m_writable->setEnabled(false);

    m_overlap = new QLabel(this);
    m_overlap->setObjectName(QStringLiteral("overlapLabel"));
    m_overlap->setStyleSheet(QStringLiteral("color: #c62828;"));
    m_overlap->setWordWrap(true);
    m_overlap->hide();

    m_readButton = new QPushButton(tr("Read"), this);
    m_readButton->setObjectName(QStringLiteral("readButton"));
    m_readButton->setAutoDefault(false);
    m_detectButton = new QPushButton(tr("Detect encoding"), this);
    m_detectButton->setObjectName(QStringLiteral("detectButton"));
    m_detectButton->setAutoDefault(false);

    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("previewLabel"));
    m_preview->setTextFormat(Qt::RichText);
    m_preview->setWordWrap(true);
    m_detect = new QLabel(this);
    m_detect->setObjectName(QStringLiteral("detectLabel"));
    m_detect->setTextFormat(Qt::RichText);
    m_detect->setWordWrap(true);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Add"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    m_autoRead = new QTimer(this);
    m_autoRead->setSingleShot(true);
    m_autoRead->setInterval(kAutoReadMs);

    auto *form = new QFormLayout;
    form->addRow(tr("Name:"), m_name);
    form->addRow(tr("Area:"), m_area);
    form->addRow(tr("Address:"), m_address);
    form->addRow(tr("Type:"), m_type);
    form->addRow(tr("Byte order:"), m_order);
    form->addRow(tr("Unit:"), m_unit);
    form->addRow(QString(), m_writable);
    auto *probeRow = new QHBoxLayout;
    probeRow->addWidget(m_readButton);
    probeRow->addWidget(m_detectButton);
    probeRow->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_overlap);
    layout->addLayout(probeRow);
    layout->addWidget(m_preview);
    layout->addWidget(m_detect);
    layout->addWidget(m_buttons);
    setMinimumWidth(440);

    connect(m_area, &QComboBox::currentIndexChanged, this, [this] { onTargetChanged(); });
    connect(m_address, &QLineEdit::textChanged, this, [this] { onTargetChanged(); });
    connect(m_type, &QComboBox::currentIndexChanged, this, [this] { onEncodingChanged(); });
    connect(m_order, &QComboBox::currentIndexChanged, this, [this] { onEncodingChanged(); });
    connect(m_readButton, &QPushButton::clicked, this, [this] { startProbe(false); });
    connect(m_detectButton, &QPushButton::clicked, this, [this] { startProbe(true); });
    connect(m_autoRead, &QTimer::timeout, this, [this] {
        if (canProbe() && !m_probing)
            startProbe(false);
    });

    refresh();
}

AddTagDialog::~AddTagDialog() = default;

void AddTagDialog::setReadFunction(ReadFn read)
{
    m_read = std::move(read);
    refresh();
}

void AddTagDialog::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    if (!connected) {
        ++m_generation;
        m_probing = false;
        m_autoRead->stop();
        m_preview->clear();
    } else {
        m_preview->clear();
    }
    refresh();
    if (connected && canProbe())
        m_autoRead->start();
}

bool AddTagDialog::usedByExisting(Area area, int first, int count) const
{
    const int last = first + count - 1;
    for (const Tag &t : std::as_const(m_existing)) {
        if (t.area != area)
            continue;
        const int tFirst = t.address;
        const int tLast = tFirst + registerCount(t.type) - 1;
        if (first <= tLast && tFirst <= last)
            return true;
    }
    return false;
}

std::optional<quint16> AddTagDialog::parsedAddress() const
{
    bool ok = false;
    const int shown = m_address->text().trimmed().toInt(&ok);
    if (!ok || shown < m_addressBase || shown > 65535 + m_addressBase)
        return std::nullopt;
    return quint16(shown - m_addressBase);
}

bool AddTagDialog::canProbe() const
{
    return m_connected && bool(m_read) && m_area->currentIndex() >= 0 && parsedAddress().has_value();
}

QString AddTagDialog::regLabel(int address) const
{
    return QStringLiteral("%1 %2")
        .arg(m_area->currentIndex() == 1 ? QStringLiteral("IR") : QStringLiteral("HR"))
        .arg(address + m_addressBase);
}

QString AddTagDialog::rangeLabel(int first, int count) const
{
    if (count <= 1)
        return regLabel(first);
    return QStringLiteral("%1-%2").arg(regLabel(first)).arg(first + count - 1 + m_addressBase);
}

QString AddTagDialog::disconnectedHint() const
{
    return rich::paragraph(tr("Connect to read the register and detect its encoding. The tag is "
                              "checked when you connect."));
}

void AddTagDialog::refresh()
{
    const bool haveArea = m_area->currentIndex() >= 0;
    const std::optional<quint16> address = parsedAddress();
    const ValueType type = ValueType(m_type->currentData().toInt());
    const int count = registerCount(type);
    const Area area = m_area->currentIndex() == 1 ? Area::InputRegister : Area::HoldingRegister;

    m_order->setEnabled(count == 2);

    const bool writableOk = m_area->currentIndex() == 0;
    m_writable->setEnabled(writableOk);
    if (!writableOk)
        m_writable->setChecked(false);

    QString overlap;
    if (haveArea && address) {
        const int first = *address;
        const int last = first + count - 1;
        if (last > 65535) {
            overlap = tr("A 32-bit value needs two registers: %1 is the last one.").arg(regLabel(65535));
        } else {
            for (const Tag &t : std::as_const(m_existing)) {
                if (t.area != area)
                    continue;
                const int tFirst = t.address;
                const int tLast = tFirst + registerCount(t.type) - 1;
                if (first <= tLast && tFirst <= last) {
                    overlap = tr("Overlaps '%1' (%2).")
                                  .arg(t.name, rangeLabel(tFirst, registerCount(t.type)));
                    break;
                }
            }
        }
    }
    m_overlap->setText(overlap);
    m_overlap->setVisible(!overlap.isEmpty());

    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(haveArea && address && overlap.isEmpty());
    const bool probeOk = canProbe() && !m_probing;
    m_readButton->setEnabled(probeOk);
    m_detectButton->setEnabled(probeOk);

    if (!m_connected || !m_read) {
        m_preview->setText(disconnectedHint());
    } else if (m_preview->text() == disconnectedHint()) {
        m_preview->clear();
    }
}

void AddTagDialog::onTargetChanged()
{
    if (m_applying) {
        refresh();
        return;
    }
    ++m_generation;
    m_probing = false;
    m_confirmed = false;
    m_autoRead->stop();
    m_detect->clear();
    m_preview->clear();
    refresh();
    if (canProbe())
        m_autoRead->start();
}

void AddTagDialog::onEncodingChanged()
{
    if (!m_applying) {
        m_confirmed = false;
        m_detect->clear();
    }
    refresh();
}

void AddTagDialog::startProbe(bool forDetect)
{
    const std::optional<quint16> address = parsedAddress();
    if (!canProbe() || m_probing || !address)
        return;
    m_autoRead->stop();
    auto state = std::make_shared<ProbeState>();
    state->area = m_area->currentIndex() == 1 ? Area::InputRegister : Area::HoldingRegister;
    state->address = *address;
    state->generation = m_generation;
    state->forDetect = forDetect;
    state->queue.push_back({ProbeState::Kind::Own, *address, 1});
    m_probing = true;
    if (forDetect) {
        m_detect->clear();
        m_confirmed = false; // a new Detect replaces any earlier answer, whatever it finds
    }
    refresh();
    probeNext(state);
}

void AddTagDialog::probeNext(const std::shared_ptr<ProbeState> &state)
{
    if (state->queue.empty()) {
        finishProbe(state);
        return;
    }
    state->current = state->queue.front();
    state->queue.pop_front();
    QPointer<AddTagDialog> self(this);
    m_read(state->area, quint16(state->current.start), state->current.count,
           [self, state](const ModbusResult &result, const QList<quint16> &values) {
               if (!self || self->m_generation != state->generation)
                   return;
               self->probeAnswered(state, result, values);
           });
}

void AddTagDialog::probeAnswered(const std::shared_ptr<ProbeState> &state, const ModbusResult &result,
                                 const QList<quint16> &values)
{
    using Kind = ProbeState::Kind;
    ProbeReadings &r = state->readings;
    const bool ok = result.ok() && values.size() >= state->current.count;
    const int address = state->address;
    switch (state->current.kind) {
    case Kind::Own:
        state->ownResult = result;
        if (ok) {
            r.own = values.at(0);
            // A neighbour used by another tag can't be the other half of this value.
            if (address > 0 && !usedByExisting(state->area, address - 1, 1))
                state->queue.push_back({Kind::Below, address - 1, 1});
            if (address < 65535 && !usedByExisting(state->area, address + 1, 1))
                state->queue.push_back({Kind::Above, address + 1, 1});
        } else if (result.error == ModbusError::Exception && result.exceptionCode == 2) {
            r.ownRefused = true;
            if (address < 65535 && !usedByExisting(state->area, address, 2))
                state->queue.push_back({Kind::PairAt, address, 2});
            if (address > 0 && !usedByExisting(state->area, address - 1, 2))
                state->queue.push_back({Kind::PairBelow, address - 1, 2});
        }
        break;
    case Kind::Below:
    case Kind::Above:
        if (ok) {
            (state->current.kind == Kind::Below ? r.below : r.above) = values.at(0);
        } else {
            r.neighboursComplete = false;
            state->unreadNeighbours.append(state->current.start);
        }
        break;
    case Kind::PairAt:
        if (ok)
            r.pairAt = RegisterPair{values.at(0), values.at(1)};
        break;
    case Kind::PairBelow:
        if (ok)
            r.pairBelow = RegisterPair{values.at(0), values.at(1)};
        break;
    }
    probeNext(state);
}

void AddTagDialog::finishProbe(const std::shared_ptr<ProbeState> &state)
{
    m_probing = false;
    m_preview->setText(previewText(*state));
    refresh();
    if (state->forDetect)
        applyDetect(*state);
}

QString AddTagDialog::previewText(const ProbeState &state) const
{
    const ProbeReadings &r = state.readings;
    const int a = state.address;
    if (!r.own && !r.ownRefused) {
        const QString message = state.ownResult.message.isEmpty() ? tr("unknown error")
                                                                  : state.ownResult.message;
        return rich::paragraph(tr("%1 could not be read: %2.").arg(regLabel(a), message));
    }

    QString html;
    QMap<int, quint16> known;
    if (r.ownRefused)
        html += rich::paragraph(tr("%1 can't be read alone (illegal data address).").arg(regLabel(a)));
    if (r.below)
        known.insert(a - 1, *r.below);
    if (r.own)
        known.insert(a, *r.own);
    if (r.above)
        known.insert(a + 1, *r.above);
    if (r.pairBelow) {
        known.insert(a - 1, r.pairBelow->r0);
        known.insert(a, r.pairBelow->r1);
    }
    if (r.pairAt) {
        known.insert(a, r.pairAt->r0);
        known.insert(a + 1, r.pairAt->r1);
    }

    QStringList regs;
    for (auto it = known.cbegin(); it != known.cend(); ++it)
        regs << QStringLiteral("%1: %2").arg(regLabel(it.key()), hex16(it.value()));
    if (!regs.isEmpty())
        html += rich::paragraph(regs.join(QStringLiteral(" · ")));

    QList<rich::Row> rows;
    if (r.own)
        rows.append({tr("%1 as int16:").arg(regLabel(a)), QString::number(qint16(*r.own)), false});
    for (int start : {a, a - 1}) {
        if (start < 0 || start + 1 > 65535 || !known.contains(start) || !known.contains(start + 1))
            continue;
        const RegisterPair pair{known.value(start), known.value(start + 1)};
        rows.append({tr("%1 as float32:").arg(rangeLabel(start, 2)),
                     QStringLiteral("%1 (ABCD), %2 (CDAB)")
                         .arg(number(decodeFloat(ByteOrder::ABCD, pair)),
                              number(decodeFloat(ByteOrder::CDAB, pair))),
                     false});
    }
    if (!rows.isEmpty())
        html += rich::rows(rows);
    return html;
}

void AddTagDialog::applyDetect(const ProbeState &state)
{
    const ProbeReadings &r = state.readings;
    const int a = state.address;
    const LayoutSuggestion s = suggestLayout(state.area, state.address, r, m_deviceOrder);

    switch (s.kind) {
    case LayoutKind::Sixteen: {
        m_applying = true;
        if (ValueType(m_type->currentData().toInt()) != ValueType::UInt16)
            m_type->setCurrentIndex(m_type->findData(int(ValueType::Int16)));
        m_applying = false;
        const quint16 own = r.own.value_or(0);
        const bool unsignedType = ValueType(m_type->currentData().toInt()) == ValueType::UInt16;
        m_detect->setText(rich::paragraph(
            tr("%1 holds a 16-bit value (%2): no float32 around it.")
                .arg(regLabel(a), unsignedType ? QString::number(own) : QString::number(qint16(own)))));
        m_confirmed = true;
        refresh();
        return;
    }
    case LayoutKind::ThirtyTwo: {
        Tag t;
        t.name = m_name->text().trimmed().isEmpty() ? tr("New tag") : m_name->text().trimmed();
        t.area = state.area;
        t.address = s.start;
        t.type = ValueType::Float32;
        t.byteOrder = s.ranking.best ? s.ranking.best->order : m_deviceOrder.value_or(ByteOrder::ABCD);
        t.unit = m_unit->text();
        QList<Tag> others;
        for (const Tag &o : std::as_const(m_existing))
            if (registerCount(o.type) == 2)
                others.append(o);

        EncodingDialog dialog(t, {s.pair}, others, m_deviceOrder, m_addressBase, this);
        const QString head = s.reason == LayoutReason::Refused
                                 ? tr("%1 can't be read alone.").arg(regLabel(a))
                                 : tr("%1 looks like half of a float32.").arg(regLabel(a));
        dialog.setIntro(rich::headline(head)
                        + rich::paragraph(tr("Choose the encoding of %1, or type the value shown on "
                                             "the device.")
                                              .arg(rangeLabel(s.start, 2))));
        if (!m_runEncodingDialog(dialog))
            return;
        const std::optional<Encoding> chosen = dialog.chosenEncoding();
        if (!chosen)
            return;
        m_applying = true;
        m_address->setText(QString::number(int(s.start) + m_addressBase));
        m_type->setCurrentIndex(m_type->findData(int(chosen->type)));
        m_order->setCurrentIndex(m_order->findData(int(chosen->order)));
        m_applying = false;
        QString text = rich::paragraph(tr("%1, %2.").arg(toString(*chosen), rangeLabel(s.start, 2)));
        if (s.start < state.address)
            text += rich::paragraph(tr("The value starts one register earlier: the address is now %1.")
                                        .arg(regLabel(s.start)));
        m_detect->setText(text);
        m_confirmed = true;
        refresh();
        return;
    }
    case LayoutKind::Unknown:
        break;
    }

    switch (s.reason) {
    case LayoutReason::OwnZero:
        m_detect->setText(rich::paragraph(
            tr("%1 reads 0: can't tell a 16-bit from a 32-bit value. Try again when the value is not "
               "0, or choose the type yourself.")
                .arg(regLabel(a))));
        break;
    case LayoutReason::NeighboursUnread: {
        QStringList unread;
        for (int n : state.unreadNeighbours)
            unread << regLabel(n);
        const quint16 own = r.own.value_or(0);
        m_detect->setText(rich::paragraph(
            tr("%1 reads %2, but %3 could not be read: a float32 can't be ruled out. Choose the type "
               "yourself; the tag is checked when it is read.")
                .arg(regLabel(a), QString::number(qint16(own)),
                     unread.join(tr(" and ")))));
        break;
    }
    case LayoutReason::RefusedNoPair:
        m_detect->setText(rich::paragraph(
            tr("%1 can't be read, alone or as part of a 32-bit value: check the register number.")
                .arg(regLabel(a))));
        break;
    default: {
        const QString message = state.ownResult.message.isEmpty() ? tr("unknown error")
                                                                  : state.ownResult.message;
        m_detect->setText(rich::paragraph(tr("%1 could not be read: %2.").arg(regLabel(a), message)));
        break;
    }
    }
}

Tag AddTagDialog::tag() const
{
    Tag t;
    t.id = 0;
    t.name = m_name->text().trimmed();
    t.area = m_area->currentIndex() == 1 ? Area::InputRegister : Area::HoldingRegister;
    t.address = parsedAddress().value_or(0);
    t.type = ValueType(m_type->currentData().toInt());
    t.byteOrder = registerCount(t.type) == 2 ? ByteOrder(m_order->currentData().toInt())
                                             : ByteOrder::ABCD;
    t.unit = m_unit->text();
    t.writable = t.area == Area::HoldingRegister && m_writable->isChecked();
    t.encodingVerified = true;
    return t;
}

bool AddTagDialog::detectionConfirmed() const
{
    return m_confirmed;
}

} // namespace mb
