#include "TagTableModel.h"

#include <QAbstractItemModelTester>
#include <QBrush>
#include <QColor>
#include <QSignalSpy>
#include <QtTest>

#include <limits>

using namespace mb;
using C = TagTableModel;

namespace {

Tag makeTag(TagId id, const QString &name, Area area = Area::HoldingRegister, quint16 addr = 0,
            bool writable = false)
{
    Tag t;
    t.id = id;
    t.name = name;
    t.area = area;
    t.address = addr;
    t.writable = writable;
    return t;
}

} // namespace

class TstTagTableModel : public QObject
{
    Q_OBJECT
private:
    static QModelIndex ix(const TagTableModel &m, int r, int c) { return m.index(r, c); }

private slots:
    void roundTripAndNextId()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        const QList<Tag> tags{makeTag(3, "a"), makeTag(7, "b", Area::InputRegister, 5)};
        m.setTags(tags, 2);
        QVERIFY(m.tags() == tags);
        QCOMPARE(m.nextTagId(), TagId(8));
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.columnCount(), int(C::ColumnCount));
        QCOMPARE(reset.size(), 1);
        QCOMPARE(edited.size(), 0);
        m.setTags(tags, 50);
        QCOMPARE(m.nextTagId(), TagId(50));
        QCOMPARE(m.rowOf(7), 1);
        QCOMPARE(m.rowOf(99), -1);
        QCOMPARE(m.tagAt(0).name, QString("a"));
    }

    void headers()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        for (int c = 0; c < C::ColumnCount; ++c)
            QVERIFY(!m.headerData(c, Qt::Horizontal).toString().isEmpty());
        QCOMPARE(m.headerData(C::Name, Qt::Horizontal).toString(), QString("Name"));
    }

    void dataAllColumns()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        Tag t = makeTag(1, "Temp", Area::HoldingRegister, 100, true);
        t.type = ValueType::UInt16;
        t.gain = 0.1;
        t.offset = -2.5;
        t.unit = "C";
        t.color = "#112233";
        t.plotted = false;
        m.setTags({t}, 2);

        QCOMPARE(m.data(ix(m, 0, C::Name)).toString(), QString("Temp"));
        QCOMPARE(m.data(ix(m, 0, C::Name), Qt::EditRole).toString(), QString("Temp"));
        QCOMPARE(m.data(ix(m, 0, C::Area)).toString(), QString("Holding"));
        QCOMPARE(m.data(ix(m, 0, C::Address)).toInt(), 100);
        QCOMPARE(m.data(ix(m, 0, C::Type)).toString(), QString("uint16"));
        QCOMPARE(m.data(ix(m, 0, C::Gain)).toDouble(), 0.1);
        QCOMPARE(m.data(ix(m, 0, C::Offset), Qt::EditRole).toDouble(), -2.5);
        QCOMPARE(m.data(ix(m, 0, C::Unit)).toString(), QString("C"));
        QCOMPARE(m.data(ix(m, 0, C::Writable), Qt::CheckStateRole).toInt(), int(Qt::Checked));
        QCOMPARE(m.data(ix(m, 0, C::Plot), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
        QCOMPARE(m.data(ix(m, 0, C::Color)).toString(), QString("#112233"));
        QCOMPARE(m.data(ix(m, 0, C::Color), Qt::EditRole).toString(), QString("#112233"));
        QCOMPARE(m.data(ix(m, 0, C::Color), Qt::DecorationRole).value<QColor>(), QColor("#112233"));
        QVERIFY(m.data(ix(m, 0, C::Raw)).toString().isEmpty());
        QVERIFY(m.data(ix(m, 0, C::Value)).toString().isEmpty());
        QCOMPARE(m.data(ix(m, 0, C::Quality)).toString(), QString("Unknown"));
        QVERIFY(!m.data(QModelIndex()).isValid());

        SampleBatch b;
        b.samples << TagSample{1, 0x00AB, 17.123456789, Quality::Good};
        m.updateSamples(b);
        QCOMPARE(m.data(ix(m, 0, C::Raw)).toString(), QString("0x00AB (171)"));
        QCOMPARE(m.data(ix(m, 0, C::Value)).toString(), QString("17.1235"));
        QCOMPARE(m.data(ix(m, 0, C::Quality)).toString(), QString("Good"));
    }

    void flagsTest()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(1, "h", Area::HoldingRegister, 0, true),
                   makeTag(2, "h2", Area::HoldingRegister, 1, false),
                   makeTag(3, "i", Area::InputRegister, 2, false)},
                  4);
        for (int r = 0; r < 3; ++r)
            for (int c : {C::Name, C::Area, C::Address, C::Type, C::Gain, C::Offset, C::Unit, C::Color})
                QVERIFY(m.flags(ix(m, r, c)) & Qt::ItemIsEditable);
        for (int r = 0; r < 3; ++r) {
            QVERIFY(m.flags(ix(m, r, C::Plot)) & Qt::ItemIsUserCheckable);
            QVERIFY(!(m.flags(ix(m, r, C::Raw)) & Qt::ItemIsEditable));
            QVERIFY(!(m.flags(ix(m, r, C::Quality)) & Qt::ItemIsEditable));
        }
        QVERIFY(m.flags(ix(m, 0, C::Writable)) & Qt::ItemIsUserCheckable);
        QVERIFY(m.flags(ix(m, 0, C::Writable)) & Qt::ItemIsEnabled);
        QVERIFY(m.flags(ix(m, 1, C::Writable)) & Qt::ItemIsUserCheckable);
        QVERIFY(!(m.flags(ix(m, 2, C::Writable)) & Qt::ItemIsEnabled));
        QVERIFY(m.flags(ix(m, 0, C::Value)) & Qt::ItemIsEditable);
        QVERIFY(!(m.flags(ix(m, 1, C::Value)) & Qt::ItemIsEditable));
        QVERIFY(!(m.flags(ix(m, 2, C::Value)) & Qt::ItemIsEditable));
    }

    void editConfigColumns()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(1, "a")}, 2);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);
        int n = 0;
        auto ok = [&](int c, const QVariant &v, int role = Qt::EditRole) {
            QVERIFY(m.setData(ix(m, 0, c), v, role));
            QCOMPARE(edited.size(), ++n);
        };
        auto bad = [&](int c, const QVariant &v, int role = Qt::EditRole) {
            QVERIFY(!m.setData(ix(m, 0, c), v, role));
            QCOMPARE(edited.size(), n);
        };

        ok(C::Name, "renamed");
        QCOMPARE(m.tagAt(0).name, QString("renamed"));
        bad(C::Name, "");
        bad(C::Name, "   ");
        QCOMPARE(m.tagAt(0).name, QString("renamed"));

        ok(C::Area, "Input");
        QVERIFY(m.tagAt(0).area == Area::InputRegister);
        bad(C::Area, "bogus");
        ok(C::Area, "Holding");

        ok(C::Address, 65535);
        QCOMPARE(m.tagAt(0).address, quint16(65535));
        ok(C::Address, 0);
        bad(C::Address, 65536);
        bad(C::Address, -1);
        bad(C::Address, "abc");

        ok(C::Type, "uint16");
        QVERIFY(m.tagAt(0).type == ValueType::UInt16);
        bad(C::Type, "float");

        ok(C::Gain, 0.5);
        QCOMPARE(m.tagAt(0).gain, 0.5);
        ok(C::Gain, "2.5");
        QCOMPARE(m.tagAt(0).gain, 2.5);
        bad(C::Gain, 0.0);
        bad(C::Gain, "x");
        bad(C::Gain, std::numeric_limits<double>::infinity());
        bad(C::Gain, std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(m.tagAt(0).gain, 2.5);

        ok(C::Offset, -3.25);
        QCOMPARE(m.tagAt(0).offset, -3.25);
        bad(C::Offset, "nope");
        bad(C::Offset, std::numeric_limits<double>::infinity());

        ok(C::Unit, "bar");
        QCOMPARE(m.tagAt(0).unit, QString("bar"));

        ok(C::Color, "#FF8800");
        QCOMPARE(m.tagAt(0).color, QString("#ff8800"));
        ok(C::Color, QColor(1, 2, 3));
        QCOMPARE(m.tagAt(0).color, QString("#010203"));
        bad(C::Color, "red");
        bad(C::Color, "#12345");
        bad(C::Color, "#GGGGGG");
        bad(C::Color, "");

        ok(C::Plot, Qt::Unchecked, Qt::CheckStateRole);
        QVERIFY(!m.tagAt(0).plotted);
        ok(C::Writable, Qt::Checked, Qt::CheckStateRole);
        QVERIFY(m.tagAt(0).writable);

        // Read-only columns and wrong roles.
        bad(C::Raw, 5);
        bad(C::Quality, "Good");
        bad(C::Name, "x", Qt::CheckStateRole);
        bad(C::Plot, Qt::Checked, Qt::EditRole);
    }

    void areaChangeClearsWritable()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(1, "a", Area::HoldingRegister, 0, true)}, 2);
        QVERIFY(m.tagAt(0).writable);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);
        QVERIFY(m.setData(ix(m, 0, C::Area), "Input"));
        QVERIFY(!m.tagAt(0).writable);
        QCOMPARE(m.data(ix(m, 0, C::Writable), Qt::CheckStateRole).toInt(), int(Qt::Unchecked));
        QCOMPARE(edited.size(), 1);
        // Cannot make an input tag writable.
        QVERIFY(!m.setData(ix(m, 0, C::Writable), Qt::Checked, Qt::CheckStateRole));
        QVERIFY(!m.tagAt(0).writable);
        QCOMPARE(edited.size(), 1);
    }

    void valueEditEmitsWriteRequest()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(5, "w", Area::HoldingRegister, 0, true),
                   makeTag(6, "ro", Area::HoldingRegister, 1, false),
                   makeTag(7, "in", Area::InputRegister, 2, false)},
                  8);
        QSignalSpy wr(&m, &TagTableModel::writeRequested);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        QVERIFY(m.setData(ix(m, 0, C::Value), 12.5));
        QCOMPARE(wr.size(), 1);
        QCOMPARE(wr.at(0).at(0).value<TagId>(), TagId(5));
        QCOMPARE(wr.at(0).at(1).toDouble(), 12.5);
        QVERIFY(m.setData(ix(m, 0, C::Value), "3.5"));
        QCOMPARE(wr.at(1).at(1).toDouble(), 3.5);
        QVERIFY(!m.setData(ix(m, 0, C::Value), "abc"));
        QCOMPARE(wr.size(), 2);
        QCOMPARE(edited.size(), 0);
        QCOMPARE(changed.size(), 0);
        QVERIFY(m.data(ix(m, 0, C::Value)).toString().isEmpty());
        QVERIFY(m.tagAt(0) == makeTag(5, "w", Area::HoldingRegister, 0, true));

        QVERIFY(!m.setData(ix(m, 1, C::Value), 1.0));
        QVERIFY(!m.setData(ix(m, 2, C::Value), 1.0));
        QCOMPARE(wr.size(), 2);
    }

    void addDuplicateRemove()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);

        int r = m.addTag();
        QCOMPARE(r, 0);
        QCOMPARE(m.tagAt(0).id, TagId(1));
        QCOMPARE(m.tagAt(0).name, QString("Tag 1"));
        QCOMPARE(m.nextTagId(), TagId(2));
        QCOMPARE(edited.size(), 1);

        // Convenience: continue after last tag.
        m.setData(ix(m, 0, C::Area), "Input");
        m.setData(ix(m, 0, C::Address), 10);
        r = m.addTag();
        QCOMPARE(r, 1);
        QVERIFY(m.tagAt(1).area == Area::InputRegister);
        QCOMPARE(m.tagAt(1).address, quint16(11));
        QCOMPARE(m.tagAt(1).name, QString("Tag 2"));

        m.setData(ix(m, 1, C::Address), 65535);
        r = m.addTag();
        QCOMPARE(m.tagAt(r).address, quint16(65535));

        // Explicit prototype is kept.
        Tag proto = makeTag(0, "mine", Area::HoldingRegister, 42, true);
        r = m.addTag(proto);
        QCOMPARE(m.tagAt(r).name, QString("mine"));
        QCOMPARE(m.tagAt(r).address, quint16(42));
        QVERIFY(m.tagAt(r).writable);
        QCOMPARE(m.tagAt(r).id, TagId(4));

        edited.clear();
        const int d = m.duplicateTag(r);
        QCOMPARE(d, m.rowCount() - 1);
        QCOMPARE(m.tagAt(d).name, QString("mine (copy)"));
        QCOMPARE(m.tagAt(d).id, TagId(5));
        QCOMPARE(m.tagAt(d).address, quint16(42));
        QCOMPARE(edited.size(), 1);
        QCOMPARE(m.duplicateTag(99), -1);
        QCOMPARE(m.nextTagId(), TagId(6));

        // Remove with live data.
        SampleBatch b;
        b.samples << TagSample{4, 1, 1.0, Quality::Good};
        m.updateSamples(b);
        edited.clear();
        QVERIFY(m.removeRows(r, 1));
        QCOMPARE(edited.size(), 1);
        QCOMPARE(m.rowOf(4), -1);
        QVERIFY(!m.removeRows(100, 1));
        QVERIFY(!m.removeRows(0, 0));
        QVERIFY(m.removeRows(0, 2));
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(edited.size(), 2);
        QCOMPARE(reset.size(), 0);
        // Ids are never reused.
        m.addTag();
        QCOMPARE(m.tags().last().id, TagId(6));
    }

    void newTagsGetDistinctPaletteColours()
    {
        const QStringList palette = {"#1f77b4", "#ff7f0e", "#2ca02c", "#d62728", "#9467bd",
                                     "#8c564b", "#e377c2", "#7f7f7f", "#bcbd22", "#17becf"};
        TagTableModel m;
        for (int i = 0; i < 10; ++i)
            QCOMPARE(m.tagAt(m.addTag()).color, palette.at(i));

        // All used: cycles.
        QCOMPARE(m.tagAt(m.addTag()).color, palette.at(0));

        // A freed colour is reused first.
        TagTableModel m2;
        for (int i = 0; i < 3; ++i)
            m2.addTag();
        QVERIFY(m2.removeRows(1, 1));
        QCOMPARE(m2.tagAt(m2.addTag()).color, palette.at(1));

        // An explicit colour is kept; duplicates keep the source colour.
        Tag proto;
        proto.color = "#123456";
        const int r = m2.addTag(proto);
        QCOMPARE(m2.tagAt(r).color, QString("#123456"));
        const int d = m2.duplicateTag(r);
        QCOMPARE(m2.tagAt(d).color, QString("#123456"));
    }

    void updateSamplesSignals()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        QList<Tag> tags;
        for (int i = 0; i < 5; ++i)
            tags << makeTag(TagId(10 + i), QString("t%1").arg(i), Area::HoldingRegister, quint16(i));
        m.setTags(tags, 20);
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);

        SampleBatch b;
        b.samples << TagSample{11, 1, 2.0, Quality::Good} << TagSample{12, 2, 3.0, Quality::Timeout}
                  << TagSample{14, 3, 4.0, Quality::Good} << TagSample{999, 0, 0, Quality::Good};
        m.updateSamples(b);
        QCOMPARE(reset.size(), 0);
        QCOMPARE(edited.size(), 0);
        QVERIFY(changed.size() >= 1);
        QSet<int> rows;
        for (const auto &args : changed) {
            const auto tl = args.at(0).value<QModelIndex>();
            const auto br = args.at(1).value<QModelIndex>();
            QCOMPARE(tl.column(), int(C::Raw));
            QCOMPARE(br.column(), int(C::Quality));
            for (int r = tl.row(); r <= br.row(); ++r)
                rows.insert(r);
        }
        QCOMPARE(rows, (QSet<int>{1, 2, 4}));
        // Row 0 and 3 untouched.
        QCOMPARE(m.data(ix(m, 0, C::Quality)).toString(), QString("Unknown"));
        QCOMPARE(m.data(ix(m, 2, C::Quality)).toString(), QString("Timeout"));
        QVERIFY(m.data(ix(m, 2, C::Value)).toString().isEmpty());
        QVERIFY(m.data(ix(m, 2, C::Raw)).toString().isEmpty());

        // Empty / unknown-only batches emit nothing.
        changed.clear();
        m.updateSamples(SampleBatch{});
        SampleBatch unk;
        unk.samples << TagSample{999, 0, 0, Quality::Good};
        m.updateSamples(unk);
        QCOMPARE(changed.size(), 0);
    }

    void qualityColoursAndTooltips()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(1, "a")}, 2);
        auto fg = [&] { return m.data(ix(m, 0, C::Quality), Qt::ForegroundRole).value<QBrush>().color(); };
        const QColor grey = fg();
        QCOMPARE(grey.red(), grey.green());
        QCOMPARE(grey.green(), grey.blue());

        SampleBatch b;
        b.samples << TagSample{1, 1, 1, Quality::Good};
        m.updateSamples(b);
        const QColor green = fg();
        QVERIFY(green.green() > green.red() && green.green() > green.blue());

        for (Quality q : {Quality::Timeout, Quality::Exception, Quality::Disconnected, Quality::Error}) {
            b.samples[0].quality = q;
            m.updateSamples(b);
            const QColor red = fg();
            QVERIFY(red.red() > red.green() && red.red() > red.blue());
            QCOMPARE(m.data(ix(m, 0, C::Quality)).toString(), toString(q));
        }

        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        m.setStatusText(1, "Illegal data address");
        QCOMPARE(m.data(ix(m, 0, C::Quality), Qt::ToolTipRole).toString(), QString("Illegal data address"));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).value<QModelIndex>(), ix(m, 0, C::Quality));
        m.setStatusText(12345, "ignored");
        QCOMPARE(changed.size(), 1);
    }

    void clearLive()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(1, "a"), makeTag(2, "b")}, 3);
        SampleBatch b;
        b.samples << TagSample{1, 1, 1, Quality::Good} << TagSample{2, 2, 2, Quality::Good};
        m.updateSamples(b);
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        m.clearLiveValues();
        QCOMPARE(reset.size(), 0);
        QVERIFY(changed.size() >= 1);
        for (int r = 0; r < 2; ++r) {
            QVERIFY(m.data(ix(m, r, C::Raw)).toString().isEmpty());
            QVERIFY(m.data(ix(m, r, C::Value)).toString().isEmpty());
            QCOMPARE(m.data(ix(m, r, C::Quality)).toString(), QString("Unknown"));
        }
    }

    void newTypesDisplayAndEdit()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(1, "a", Area::HoldingRegister, 0)}, 2);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);

        QVERIFY(m.setData(ix(m, 0, C::Type), "int32"));
        QVERIFY(m.tagAt(0).type == ValueType::Int32);
        QCOMPARE(m.data(ix(m, 0, C::Type)).toString(), QString("int32"));
        QVERIFY(m.setData(ix(m, 0, C::Type), "UINT32"));
        QVERIFY(m.tagAt(0).type == ValueType::UInt32);
        QCOMPARE(m.data(ix(m, 0, C::Type)).toString(), QString("uint32"));
        QVERIFY(m.setData(ix(m, 0, C::Type), "float32"));
        QVERIFY(m.tagAt(0).type == ValueType::Float32);
        QCOMPARE(m.data(ix(m, 0, C::Type), Qt::EditRole).toString(), QString("float32"));
        QVERIFY(m.setData(ix(m, 0, C::Type), "int16"));
        QCOMPARE(m.data(ix(m, 0, C::Type)).toString(), QString("int16"));
        QVERIFY(!m.setData(ix(m, 0, C::Type), "double"));
        QVERIFY(!m.setData(ix(m, 0, C::Type), "float"));
        QVERIFY(m.tagAt(0).type == ValueType::Int16);
        QCOMPARE(edited.size(), 4);
    }

    void byteOrderColumn()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        Tag t16 = makeTag(1, "s16", Area::HoldingRegister, 0);
        Tag t32 = makeTag(2, "f32", Area::HoldingRegister, 2);
        t32.type = ValueType::Float32;
        m.setTags({t16, t32}, 3);
        const QString dash(QChar(0x2014));

        // 16-bit: dash, not editable, explained in the tooltip.
        QCOMPARE(m.data(ix(m, 0, C::ByteOrder)).toString(), dash);
        QCOMPARE(m.data(ix(m, 0, C::ByteOrder), Qt::ToolTipRole).toString(),
                 QString("Only used by 32-bit types"));
        QVERIFY(!(m.flags(ix(m, 0, C::ByteOrder)) & Qt::ItemIsEditable));
        QVERIFY(!m.setData(ix(m, 0, C::ByteOrder), "CDAB"));
        QVERIFY(m.tagAt(0).byteOrder == ByteOrder::ABCD);

        // 32-bit: shows and edits the order.
        QCOMPARE(m.data(ix(m, 1, C::ByteOrder)).toString(), QString("ABCD"));
        QVERIFY(m.flags(ix(m, 1, C::ByteOrder)) & Qt::ItemIsEditable);
        QVERIFY(m.setData(ix(m, 1, C::ByteOrder), "CDAB"));
        QVERIFY(m.tagAt(1).byteOrder == ByteOrder::CDAB);
        QCOMPARE(m.data(ix(m, 1, C::ByteOrder)).toString(), QString("CDAB"));
        QVERIFY(m.setData(ix(m, 1, C::ByteOrder), "badc"));
        QVERIFY(m.tagAt(1).byteOrder == ByteOrder::BADC);
        QVERIFY(!m.setData(ix(m, 1, C::ByteOrder), "ABCDE"));
        QVERIFY(!m.setData(ix(m, 1, C::ByteOrder), "XY"));
        QVERIFY(m.tagAt(1).byteOrder == ByteOrder::BADC);
        QCOMPARE(m.data(ix(m, 1, C::ByteOrder), Qt::ToolTipRole).toString(), QString());
    }

    void typeChangeTo32BitKeepsWritable()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        m.setTags({makeTag(1, "w", Area::HoldingRegister, 10, true)}, 2);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);
        QVERIFY(m.setData(ix(m, 0, C::Type), "float32"));
        QVERIFY(m.tagAt(0).isWritable());
        QCOMPARE(m.data(ix(m, 0, C::Writable), Qt::CheckStateRole).toInt(), int(Qt::Checked));
        QCOMPARE(edited.size(), 1);
    }

    void address65535Rules()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        Tag f = makeTag(2, "f", Area::HoldingRegister, 10);
        m.setTags({makeTag(1, "top", Area::HoldingRegister, 65535), f}, 3);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);

        // A 16-bit tag at 65535 cannot become 32-bit.
        QVERIFY(!m.setData(ix(m, 0, C::Type), "float32"));
        QVERIFY(!m.setData(ix(m, 0, C::Type), "uint32"));
        QVERIFY(m.tagAt(0).type == ValueType::Int16);
        QCOMPARE(edited.size(), 0);

        // A 32-bit tag cannot move to 65535; 65534 is the last valid address.
        QVERIFY(m.setData(ix(m, 1, C::Type), "float32"));
        QVERIFY(!m.setData(ix(m, 1, C::Address), 65535));
        QCOMPARE(m.tagAt(1).address, quint16(10));
        QVERIFY(m.setData(ix(m, 1, C::Address), 65534));
        QCOMPARE(m.tagAt(1).address, quint16(65534));
        QCOMPARE(edited.size(), 2);

        // A 16-bit tag may use 65535.
        QVERIFY(m.setData(ix(m, 0, C::Address), 65535));
        QCOMPARE(m.tagAt(0).address, quint16(65535));
    }

    void writableFor32BitHolding()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        Tag t = makeTag(1, "f", Area::HoldingRegister, 6638, true);
        t.type = ValueType::Float32;
        m.setTags({t}, 2);
        QVERIFY(m.flags(ix(m, 0, C::Writable)) & Qt::ItemIsEnabled);
        QCOMPARE(m.data(ix(m, 0, C::Writable), Qt::CheckStateRole).toInt(), int(Qt::Checked));
        QVERIFY(m.flags(ix(m, 0, C::Value)) & Qt::ItemIsEditable);
        QSignalSpy writes(&m, &TagTableModel::writeRequested);
        QVERIFY(m.setData(ix(m, 0, C::Value), 50.5));
        QCOMPARE(writes.size(), 1);
        QCOMPARE(writes.at(0).at(1).toDouble(), 50.5);
        QVERIFY(m.setData(ix(m, 0, C::Writable), Qt::Unchecked, Qt::CheckStateRole));
        QVERIFY(!(m.flags(ix(m, 0, C::Value)) & Qt::ItemIsEditable));
    }

    void raw32AndFloatFormatting()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        Tag t = makeTag(1, "f", Area::InputRegister, 20);
        t.type = ValueType::Float32;
        m.setTags({t, makeTag(2, "s", Area::InputRegister, 30)}, 3);

        SampleBatch b;
        b.samples << TagSample{1, 0x42F6E979u, 123.456, Quality::Good}
                  << TagSample{2, 0x0000FFFFu, -1.0, Quality::Good};
        m.updateSamples(b);
        QCOMPARE(m.data(ix(m, 0, C::Raw)).toString(), QString("0x42F6E979"));
        QCOMPARE(m.data(ix(m, 0, C::Value)).toString(), QString("123.456"));
        // 16-bit rows keep the 16-bit format.
        QCOMPARE(m.data(ix(m, 1, C::Raw)).toString(), QString("0xFFFF (65535)"));

        b.samples[0].value = 1.0 / 3.0;
        m.updateSamples(b);
        QCOMPARE(m.data(ix(m, 0, C::Value)).toString(), QString("0.3333333"));

        // Not Good: no raw, no value.
        b.samples[0].quality = Quality::Timeout;
        m.updateSamples(b);
        QVERIFY(m.data(ix(m, 0, C::Raw)).toString().isEmpty());
        QVERIFY(m.data(ix(m, 0, C::Value)).toString().isEmpty());
    }

    void addressBaseDisplayAndEdit()
    {
        TagTableModel m;
        QAbstractItemModelTester tester(&m);
        QCOMPARE(m.addressBase(), 0);
        Tag f = makeTag(2, "f", Area::HoldingRegister, 6638);
        f.type = ValueType::Float32;
        m.setTags({makeTag(1, "a", Area::InputRegister, 5008), f}, 3);
        QSignalSpy edited(&m, &TagTableModel::tagsEdited);
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        QSignalSpy headerChanged(&m, &QAbstractItemModel::headerDataChanged);

        QCOMPARE(m.headerData(C::Address, Qt::Horizontal).toString(), QString("Address"));
        m.setAddressBase(1);
        QCOMPARE(m.addressBase(), 1);
        QCOMPARE(edited.size(), 0);
        QVERIFY(headerChanged.size() >= 1);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).toModelIndex(), ix(m, 0, C::Address));
        QCOMPARE(changed.at(0).at(1).toModelIndex(), ix(m, 1, C::Address));

        QCOMPARE(m.headerData(C::Address, Qt::Horizontal).toString(), QString("Register"));
        QVERIFY(!m.headerData(C::Address, Qt::Horizontal, Qt::ToolTipRole).toString().isEmpty());
        QCOMPARE(m.data(ix(m, 0, C::Address)).toInt(), 5009);
        QCOMPARE(m.data(ix(m, 0, C::Address), Qt::EditRole).toInt(), 5009);
        QCOMPARE(m.data(ix(m, 1, C::Address)).toInt(), 6639);
        QCOMPARE(m.tagAt(0).address, quint16(5008)); // stored zero-based

        // Edit: the shown number is stored minus 1; 16-bit range 1..65536.
        QVERIFY(m.setData(ix(m, 0, C::Address), 5010));
        QCOMPARE(m.tagAt(0).address, quint16(5009));
        QVERIFY(m.setData(ix(m, 0, C::Address), 65536));
        QCOMPARE(m.tagAt(0).address, quint16(65535));
        QVERIFY(!m.setData(ix(m, 0, C::Address), 65537));
        QVERIFY(!m.setData(ix(m, 0, C::Address), 0));
        QVERIFY(m.setData(ix(m, 0, C::Address), 1));
        QCOMPARE(m.tagAt(0).address, quint16(0));

        // 32-bit: 1..65535.
        QVERIFY(m.setData(ix(m, 1, C::Address), 65535));
        QCOMPARE(m.tagAt(1).address, quint16(65534));
        QVERIFY(!m.setData(ix(m, 1, C::Address), 65536));
        QCOMPARE(m.tagAt(1).address, quint16(65534));
        QVERIFY(!m.setData(ix(m, 1, C::Address), 0));
        QVERIFY(m.setData(ix(m, 1, C::Address), 1));
        QCOMPARE(m.tagAt(1).address, quint16(0));

        // Back to base 0.
        m.setAddressBase(0);
        QCOMPARE(m.headerData(C::Address, Qt::Horizontal).toString(), QString("Address"));
        QCOMPARE(m.data(ix(m, 0, C::Address)).toInt(), 0);
        QVERIFY(!m.setData(ix(m, 0, C::Address), 65536));
    }

    void addressBaseSameValueEmitsNothing()
    {
        TagTableModel m;
        m.setTags({makeTag(1, "a")}, 2);
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        m.setAddressBase(0);
        QCOMPARE(changed.size(), 0);
    }
    void headerTooltips()
    {
        TagTableModel m;
        QVERIFY(m.headerData(C::Type, Qt::Horizontal, Qt::ToolTipRole).toString().contains("two consecutive registers"));
        QVERIFY(m.headerData(C::ByteOrder, Qt::Horizontal, Qt::ToolTipRole).toString().contains("ABCD"));
        QCOMPARE(m.headerData(C::ByteOrder, Qt::Horizontal).toString(), QString("Byte order"));
    }
};

QTEST_MAIN(TstTagTableModel)
#include "tst_tagtablemodel.moc"

