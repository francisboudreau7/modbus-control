#include "EncodingDialog.h"
#include "EncodingMonitor.h"
#include "MetaTypes.h"
#include "TagCodec.h"
#include "TagTableModel.h"

#include <QSignalSpy>
#include <QtTest>

#include <functional>
#include <memory>

using namespace mb;

namespace {

constexpr Encoding kF_ABCD{ValueType::Float32, ByteOrder::ABCD};
constexpr Encoding kF_CDAB{ValueType::Float32, ByteOrder::CDAB};
constexpr Encoding kI_ABCD{ValueType::Int32, ByteOrder::ABCD};

Tag mkTag(const QString &name, quint16 address, ValueType type = ValueType::Float32,
          ByteOrder order = ByteOrder::ABCD, const QString &unit = {})
{
    Tag t;
    t.name = name;
    t.area = Area::HoldingRegister;
    t.address = address;
    t.type = type;
    t.byteOrder = order;
    t.unit = unit;
    return t;
}

} // namespace

class TstEncodingMonitor : public QObject
{
    Q_OBJECT

    std::unique_ptr<TagTableModel> model;
    std::unique_ptr<EncodingMonitor> mon;
    std::unique_ptr<QSignalSpy> messages;

    QStringList dialogs;                            ///< window titles of the opened dialogs
    std::function<bool(EncodingDialog &)> onDialog; ///< unset: unexpected, cancelled
    QStringList reviews;                            ///< review prompt texts
    bool reviewAnswer = false;
    QStringList unexpected;

    // ---- helpers -------------------------------------------------------------------------

    /// Adds a tag as the user would (the model emits tagsEdited, the monitor hears it).
    TagId add(const Tag &prototype) { return model->tagAt(model->addTag(prototype)).id; }

    /// Replaces the whole list (like opening a project); ids are 1, 2, ... unless set.
    void load(QList<Tag> tags)
    {
        TagId next = 1;
        for (int i = 0; i < tags.size(); ++i) {
            if (tags[i].id == 0)
                tags[i].id = TagId(i + 1);
            next = qMax(next, TagId(tags[i].id + 1));
        }
        model->setTags(tags, next);
        mon->onTagsReplaced();
    }

    const Tag &tagOf(TagId id) const { return model->tagAt(model->rowOf(id)); }

    /// A sample of `value` as a device with encoding `truth` sends it, decoded by the tag as
    /// currently configured in the model.
    TagSample sample(TagId id, Encoding truth, double value, Quality quality = Quality::Good) const
    {
        const Tag configured = tagOf(id);
        Tag device = configured;
        device.type = truth.type;
        device.byteOrder = truth.order;
        const auto regs = encodeRegisters(device, value);
        TagSample s;
        s.tagId = id;
        s.quality = quality;
        if (regs) {
            s.raw = combineRegisters(configured, regs->at(0), regs->at(1));
            s.value = decodeValue(configured, s.raw);
        }
        return s;
    }

    void feed(const QList<TagSample> &samples)
    {
        SampleBatch b;
        b.samples = samples;
        mon->onSamples(b);
    }

    void feedOne(TagId id, Encoding truth, double value, Quality quality = Quality::Good)
    {
        feed({sample(id, truth, value, quality)});
    }

    /// Readings 52.1, 52.2, 52.3 of a device with encoding `truth`.
    void feedThree(TagId id, Encoding truth)
    {
        feedOne(id, truth, 52.1);
        feedOne(id, truth, 52.2);
        feedOne(id, truth, 52.3);
    }

    static QString g7(double v) { return QString::number(v, 'g', 7); }

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("ModbusControlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_encodingmonitor"));
        registerMetaTypes();
    }

    void init()
    {
        model = std::make_unique<TagTableModel>();
        mon = std::make_unique<EncodingMonitor>(model.get(), nullptr);
        connect(model.get(), &TagTableModel::tagsEdited, mon.get(), &EncodingMonitor::onTagsChanged);
        messages = std::make_unique<QSignalSpy>(mon.get(), &EncodingMonitor::message);
        dialogs.clear();
        onDialog = nullptr;
        reviews.clear();
        reviewAnswer = false;
        unexpected.clear();
        mon->m_runDialog = [this](EncodingDialog &dialog) {
            dialogs << dialog.windowTitle();
            if (!onDialog) {
                unexpected << QStringLiteral("dialog: ") + dialog.windowTitle();
                return false;
            }
            return onDialog(dialog);
        };
        mon->m_reviewPrompt = [this](const QString &text) {
            reviews << text;
            return reviewAnswer;
        };
    }

    void cleanup()
    {
        messages.reset();
        mon.reset();
        model.reset();
        QVERIFY2(unexpected.isEmpty(), qPrintable(unexpected.join(QLatin1Char('\n'))));
    }

    // ---- new tags ------------------------------------------------------------------------

    void clearDetectionChangesOrder()
    {
        mon->onConnected();
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110, ValueType::Float32, ByteOrder::ABCD,
                                   QStringLiteral("°C")));
        QVERIFY(mon->isPending(id));
        feedOne(id, kF_CDAB, 52.1);
        feedOne(id, kF_CDAB, 52.2);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD); // two readings are not enough
        QVERIFY(messages->isEmpty());
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(!tagOf(id).encodingVerified);
        QVERIFY(!mon->isPending(id));
        QCOMPARE(messages->size(), 1);
        QCOMPARE(messages->first().first().toString(),
                 QStringLiteral("Detected float32 CDAB for 'Temp' (reads 52.3 °C). "
                                "Confirm it before writing."));
        // The monitor's own change is not a new trigger, and more readings change nothing.
        feedThree(id, kF_CDAB);
        QCOMPARE(messages->size(), 1);
        QVERIFY(dialogs.isEmpty());
    }

    void clearSameEncodingChangesNothing()
    {
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110, ValueType::Float32, ByteOrder::CDAB));
        feedThree(id, kF_CDAB);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(!mon->isPending(id));
        QVERIFY(messages->isEmpty());
        QVERIFY(dialogs.isEmpty());
    }

    void ambiguousOpensDialogAccepted()
    {
        const TagId id = add(mkTag(QStringLiteral("Counter"), 110, ValueType::Float32, ByteOrder::CDAB));
        int size = -1;
        EncodingVerdict verdict = EncodingVerdict::Clear;
        onDialog = [&](EncodingDialog &dlg) {
            size = int(dlg.result().candidates.first().values.size());
            verdict = dlg.result().verdict;
            dlg.setDisplayValue(QStringLiteral("100"));
            return dlg.chosenEncoding() == std::optional<Encoding>(kI_ABCD);
        };
        feedOne(id, kI_ABCD, 100);
        feedOne(id, kI_ABCD, 100);
        QVERIFY(dialogs.isEmpty());
        feedOne(id, kI_ABCD, 100);
        QCOMPARE(dialogs, QStringList{QStringLiteral("Detect encoding - Counter")});
        QCOMPARE(size, 3);
        QVERIFY(verdict == EncodingVerdict::Ambiguous);
        QVERIFY(tagOf(id).type == ValueType::Int32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(!mon->isPending(id));
        feedThree(id, kI_ABCD);
        QCOMPARE(dialogs.size(), 1);
    }

    // A new 32-bit tag is unconfirmed until detection has a result; readings that fit its own
    // encoding confirm it.
    void newTagUnconfirmedUntilReadingsAgree()
    {
        const TagId id = add(mkTag(QStringLiteral("Sp"), 110, ValueType::Float32, ByteOrder::CDAB));
        QVERIFY(mon->isPending(id));
        QVERIFY(!tagOf(id).encodingVerified);
        feedThree(id, kF_CDAB);
        QVERIFY(!mon->isPending(id));
        QVERIFY(tagOf(id).encodingVerified);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(dialogs.isEmpty());
        QVERIFY(messages->isEmpty());
    }

    // Registers at 0 never decide: the tag stays pending and unconfirmed (writes ask first).
    void zerosKeepTagUnconfirmed()
    {
        const TagId id = add(mkTag(QStringLiteral("Sp"), 110, ValueType::Float32, ByteOrder::ABCD));
        for (int i = 0; i < 6; ++i)
            feedOne(id, kF_CDAB, 0.0);
        QVERIFY(mon->isPending(id));
        QVERIFY(!tagOf(id).encodingVerified);
        QVERIFY(dialogs.isEmpty());
    }

    // A plausible current reading is not overridden: int32 17000 (0x0000 0x4268) also reads as
    // float32 58.0 in CDAB order; the user decides.
    void plausibleCurrentEncodingAsks()
    {
        const TagId id = add(mkTag(QStringLiteral("Count"), 110, ValueType::Int32, ByteOrder::ABCD));
        onDialog = [](EncodingDialog &) { return false; };
        for (int i = 0; i < 3; ++i)
            feedOne(id, kI_ABCD, 17000);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(id).type == ValueType::Int32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(messages->isEmpty());
    }

    void ambiguousCancelled()
    {
        const TagId id = add(mkTag(QStringLiteral("Counter"), 110, ValueType::Float32, ByteOrder::CDAB));
        onDialog = [](EncodingDialog &) { return false; };
        for (int i = 0; i < 3; ++i)
            feedOne(id, kI_ABCD, 100);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(tagOf(id).type == ValueType::Float32);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(!tagOf(id).encodingVerified); // still to be confirmed
        QVERIFY(!mon->isPending(id));
        for (int i = 0; i < 3; ++i)
            feedOne(id, kI_ABCD, 100);
        QCOMPARE(dialogs.size(), 1);
    }

    void undecidableStaysPending()
    {
        const TagId id = add(mkTag(QStringLiteral("Idle"), 110));
        for (int i = 0; i < 5; ++i)
            feedOne(id, kF_CDAB, 0.0);
        QVERIFY(mon->isPending(id));
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(messages->isEmpty());
        QVERIFY(dialogs.isEmpty());
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(!mon->isPending(id));
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
        QVERIFY(!tagOf(id).encodingVerified);
        QCOMPARE(messages->size(), 1);
    }

    void onlyGoodSamplesCount()
    {
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110));
        for (int i = 0; i < 6; ++i)
            feedOne(id, kF_CDAB, 52.0 + i, Quality::Timeout);
        feedOne(id, kF_CDAB, 52.1);
        feedOne(id, kF_CDAB, 52.2, Quality::Exception);
        feedOne(id, kF_CDAB, 52.2);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD);
        QVERIFY(mon->isPending(id));
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
    }

    // ---- what makes a tag pending ----------------------------------------------------------

    void pendingRules()
    {
        // 16-bit tags never.
        const TagId i16 = add(mkTag(QStringLiteral("Small"), 5, ValueType::Int16));
        QVERIFY(!mon->isPending(i16));
        // 16-bit -> 32-bit: pending.
        model->setData(model->index(model->rowOf(i16), TagTableModel::Type), QStringLiteral("float32"));
        QVERIFY(mon->isPending(i16));
        // A type change among 32-bit types, or a byte order change alone, ends it.
        model->setData(model->index(model->rowOf(i16), TagTableModel::Type), QStringLiteral("int32"));
        QVERIFY(!mon->isPending(i16));

        load({mkTag(QStringLiteral("A"), 100), mkTag(QStringLiteral("B"), 110, ValueType::Int32)});
        const TagId a = 1;
        const TagId b = 2;
        QVERIFY(!mon->isPending(a));
        QVERIFY(!mon->isPending(b));
        model->setData(model->index(model->rowOf(a), TagTableModel::ByteOrder), QStringLiteral("CDAB"));
        QVERIFY(!mon->isPending(a));
        model->setData(model->index(model->rowOf(a), TagTableModel::Type), QStringLiteral("uint32"));
        QVERIFY(!mon->isPending(a));
        // Address (and area) change: pending.
        model->setData(model->index(model->rowOf(a), TagTableModel::Address), 120);
        QVERIFY(mon->isPending(a));
        model->setData(model->index(model->rowOf(b), TagTableModel::Area), QStringLiteral("Input"));
        QVERIFY(mon->isPending(b));
        // Replacing the list clears everything.
        mon->onTagsReplaced();
        QVERIFY(!mon->isPending(a));
        QVERIFY(!mon->isPending(b));
        // Removing a pending tag forgets it.
        model->setData(model->index(model->rowOf(a), TagTableModel::Address), 130);
        QVERIFY(mon->isPending(a));
        model->removeRows(model->rowOf(a), 1);
        QVERIFY(!mon->isPending(a));
    }

    // ---- on-connect check ------------------------------------------------------------------

    void connectCheckFlagsSuspiciousTag()
    {
        load({mkTag(QStringLiteral("P"), 110, ValueType::Float32, ByteOrder::ABCD, QStringLiteral("bar")),
              mkTag(QStringLiteral("Q"), 120, ValueType::Float32, ByteOrder::ABCD)});
        mon->onConnected();
        for (int i = 0; i < 3; ++i) {
            QVERIFY(reviews.isEmpty());
            feed({sample(1, kF_CDAB, 52.1 + 0.1 * i), sample(2, kF_ABCD, 10.0 + i)});
        }
        QCOMPARE(reviews.size(), 1);
        const Tag p = tagOf(1);
        const quint32 raw = sample(1, kF_CDAB, 52.1 + 0.2).raw;
        const QString expected =
            QStringLiteral("These tags read implausible values with their current encoding: "
                           "'P' reads %1 bar as float32 ABCD (float32 CDAB gives %2 bar). "
                           "Writes to them are blocked until you confirm their encoding.")
                .arg(g7(decodeValue(p, raw)), g7(52.3));
        QCOMPARE(reviews.first(), expected);
        QVERIFY(!tagOf(1).encodingVerified);
        QVERIFY(tagOf(1).byteOrder == ByteOrder::ABCD);
        QVERIFY(tagOf(2).encodingVerified);
        QVERIFY(dialogs.isEmpty());
        // Once only.
        feed({sample(1, kF_CDAB, 52.5), sample(2, kF_ABCD, 14.0)});
        QCOMPARE(reviews.size(), 1);
    }

    void connectCheckReviewOpensDialog()
    {
        load({mkTag(QStringLiteral("P"), 110, ValueType::Float32, ByteOrder::ABCD)});
        reviewAnswer = true;
        onDialog = [](EncodingDialog &) { return false; };
        mon->onConnected();
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        QCOMPARE(reviews.size(), 1);
        QCOMPARE(dialogs, QStringList{QStringLiteral("Detect encoding - P")});
    }

    void connectCheckSkipsSilentTagsAfterTenBatches()
    {
        load({mkTag(QStringLiteral("Silent"), 100), mkTag(QStringLiteral("P"), 110)});
        mon->onConnected();
        for (int i = 0; i < 9; ++i) {
            feed({sample(1, kF_ABCD, 1.0, Quality::Timeout), sample(2, kF_CDAB, 52.1 + 0.1 * i)});
            QVERIFY(reviews.isEmpty());
        }
        feed({sample(1, kF_ABCD, 1.0, Quality::Timeout), sample(2, kF_CDAB, 53.0)});
        QCOMPARE(reviews.size(), 1);
        QVERIFY(reviews.first().contains(QStringLiteral("'P' reads")));
        QVERIFY(!reviews.first().contains(QStringLiteral("Silent")));
        QVERIFY(tagOf(1).encodingVerified);
        QVERIFY(!tagOf(2).encodingVerified);
    }

    void connectCheckIgnoresPendingTags()
    {
        load({mkTag(QStringLiteral("P"), 110)});
        mon->onConnected();
        const TagId extra = add(mkTag(QStringLiteral("New"), 120));
        QVERIFY(mon->isPending(extra));
        for (int i = 0; i < 3; ++i)
            feed({sample(1, kF_ABCD, 52.1 + 0.1 * i), sample(extra, kF_CDAB, 52.1 + 0.1 * i)});
        QVERIFY(reviews.isEmpty());
        QVERIFY(tagOf(extra).byteOrder == ByteOrder::CDAB); // handled as a new tag
    }

    void disconnectForgetsReadings()
    {
        const TagId id = add(mkTag(QStringLiteral("Temp"), 110));
        mon->onConnected();
        feedOne(id, kF_CDAB, 52.1);
        feedOne(id, kF_CDAB, 52.2);
        mon->onDisconnected();
        QVERIFY(mon->isPending(id));
        mon->onConnected();
        feedOne(id, kF_CDAB, 52.3);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::ABCD); // the two earlier readings are gone
        feedOne(id, kF_CDAB, 52.4);
        feedOne(id, kF_CDAB, 52.5);
        QVERIFY(tagOf(id).byteOrder == ByteOrder::CDAB);
    }

    // ---- device order ------------------------------------------------------------------------

    void deviceOrder()
    {
        QVERIFY(!mon->deviceOrder());
        Tag a = mkTag(QStringLiteral("A"), 100, ValueType::Float32, ByteOrder::CDAB);
        Tag b = mkTag(QStringLiteral("B"), 110, ValueType::Int32, ByteOrder::CDAB);
        Tag c = mkTag(QStringLiteral("C"), 120, ValueType::Float32, ByteOrder::ABCD);
        Tag s = mkTag(QStringLiteral("S"), 5, ValueType::Int16, ByteOrder::BADC);
        load({s});
        QVERIFY(!mon->deviceOrder());
        load({a, s});
        QVERIFY(mon->deviceOrder() == std::optional<ByteOrder>(ByteOrder::CDAB));
        load({a, b, s});
        QVERIFY(mon->deviceOrder() == std::optional<ByteOrder>(ByteOrder::CDAB));
        load({a, b, c});
        QVERIFY(!mon->deviceOrder());
        c.encodingVerified = false; // unverified tags do not vote
        load({a, b, c});
        QVERIFY(mon->deviceOrder() == std::optional<ByteOrder>(ByteOrder::CDAB));
        a.encodingVerified = false;
        b.encodingVerified = false;
        load({a, b, c});
        QVERIFY(!mon->deviceOrder());
    }

    // ---- detectNow ---------------------------------------------------------------------------

    void detectNowUsesRecentAndLiveReadings()
    {
        load({mkTag(QStringLiteral("T"), 110)});
        for (int i = 0; i < 4; ++i)
            feedOne(1, kF_ABCD, 20.0 + i);
        int before = -1;
        int after = -1;
        onDialog = [&](EncodingDialog &dlg) {
            before = int(dlg.result().candidates.first().values.size());
            feedOne(1, kF_ABCD, 30.0);
            after = int(dlg.result().candidates.first().values.size());
            return false;
        };
        mon->detectNow(1);
        QCOMPARE(dialogs, QStringList{QStringLiteral("Detect encoding - T")});
        QCOMPARE(before, 4);
        QCOMPARE(after, 5);
    }

    void detectNowKeepsTenReadings()
    {
        load({mkTag(QStringLiteral("T"), 110)});
        for (int i = 0; i < 15; ++i)
            feedOne(1, kF_ABCD, 20.0 + i);
        int size = -1;
        onDialog = [&](EncodingDialog &dlg) {
            size = int(dlg.result().candidates.first().values.size());
            return false;
        };
        mon->detectNow(1);
        QCOMPARE(size, 10);
    }

    void detectNowIgnoresUnknownAndSmallTags()
    {
        load({mkTag(QStringLiteral("S"), 5, ValueType::Int16)});
        mon->detectNow(1);
        mon->detectNow(99);
        QVERIFY(dialogs.isEmpty());
    }

    void acceptedAppliesOrderToOthers()
    {
        Tag other = mkTag(QStringLiteral("Other"), 130, ValueType::Int32, ByteOrder::ABCD);
        other.encodingVerified = false;
        Tag third = mkTag(QStringLiteral("Third"), 140, ValueType::Float32, ByteOrder::ABCD);
        load({mkTag(QStringLiteral("T"), 110), other, third, mkTag(QStringLiteral("S"), 5, ValueType::Int16)});
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        QVERIFY(tagOf(1).byteOrder == ByteOrder::ABCD); // loaded tag: no automatic detection
        QList<TagId> others;
        onDialog = [&](EncodingDialog &dlg) {
            others = dlg.applyOrderTo();
            return dlg.chosenEncoding() == std::optional<Encoding>(kF_CDAB);
        };
        mon->detectNow(1);
        QCOMPARE(others, QList<TagId>{2}); // only the unconfirmed tag is offered
        QVERIFY(tagOf(1).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(1).type == ValueType::Float32);
        QVERIFY(tagOf(1).encodingVerified);
        QVERIFY(tagOf(2).byteOrder == ByteOrder::CDAB);
        QVERIFY(tagOf(2).type == ValueType::Int32);
        QVERIFY(!tagOf(2).encodingVerified); // its order was chosen for it: still to be confirmed
        QVERIFY(tagOf(3).byteOrder == ByteOrder::ABCD); // confirmed: never changed
        QVERIFY(tagOf(3).type == ValueType::Float32);
        QVERIFY(tagOf(3).encodingVerified);
        QVERIFY(tagOf(4).byteOrder == ByteOrder::ABCD);
        QVERIFY(!mon->isPending(1));
        QVERIFY(!mon->isPending(2));
        QVERIFY(!mon->isPending(3));
    }

    // ---- one dialog at a time ----------------------------------------------------------------

    void dialogsAreQueued()
    {
        const TagId a = add(mkTag(QStringLiteral("A"), 110, ValueType::Float32, ByteOrder::CDAB));
        const TagId b = add(mkTag(QStringLiteral("B"), 120, ValueType::Float32, ByteOrder::CDAB));
        int depth = 0;
        int maxDepth = 0;
        QStringList order;
        int liveSize = -1;
        int openedWhileFirst = -1;
        onDialog = [&](EncodingDialog &dlg) {
            ++depth;
            maxDepth = qMax(maxDepth, depth);
            order << dlg.windowTitle();
            if (order.size() == 1) {
                // Samples keep arriving while the first dialog is open.
                feed({sample(a, kI_ABCD, 100), sample(b, kI_ABCD, 100)});
                liveSize = int(dlg.result().candidates.first().values.size());
                openedWhileFirst = int(dialogs.size());
            }
            --depth;
            return true;
        };
        for (int i = 0; i < 3; ++i)
            feed({sample(a, kI_ABCD, 100), sample(b, kI_ABCD, 100)});
        QCOMPARE(order, (QStringList{QStringLiteral("Detect encoding - A"),
                                     QStringLiteral("Detect encoding - B")}));
        QCOMPARE(maxDepth, 1);
        QCOMPARE(liveSize, 4);
        QCOMPARE(openedWhileFirst, 1);
        QVERIFY(!mon->isPending(a));
        QVERIFY(!mon->isPending(b));
    }

    void tagRemovedWhileDialogOpen()
    {
        load({mkTag(QStringLiteral("T"), 110), mkTag(QStringLiteral("U"), 120, ValueType::Int32)});
        onDialog = [&](EncodingDialog &) {
            model->removeRows(model->rowOf(1), 1);
            return true;
        };
        mon->detectNow(1);
        QCOMPARE(dialogs.size(), 1);
        QCOMPARE(model->rowCount(), 1);
        QVERIFY(tagOf(2).type == ValueType::Int32);
        QVERIFY(messages->isEmpty());
    }

    void tagReconfiguredWhileDialogOpen()
    {
        load({mkTag(QStringLiteral("T"), 110)});
        for (int i = 0; i < 3; ++i)
            feedOne(1, kF_CDAB, 52.1 + 0.1 * i);
        bool chosen = false;
        onDialog = [&](EncodingDialog &dlg) {
            model->setData(model->index(0, TagTableModel::Address), 150);
            chosen = dlg.chosenEncoding() == std::optional<Encoding>(kF_CDAB);
            return true;
        };
        mon->detectNow(1);
        QCOMPARE(dialogs.size(), 1);
        QVERIFY(chosen);
        QVERIFY(tagOf(1).byteOrder == ByteOrder::ABCD); // not applied to the new address
        QVERIFY(!tagOf(1).encodingVerified);             // re-addressed: to be confirmed
        QVERIFY(mon->isPending(1));
    }
};

QTEST_MAIN(TstEncodingMonitor)
#include "tst_encodingmonitor.moc"
