#include "RequestLog.h"

#include <QtTest>

#include <string>

using namespace mbsim;

class TstRequestLog : public QObject
{
    Q_OBJECT
private slots:
    void writeFunctions()
    {
        for (unsigned fc : {0x05u, 0x06u, 0x0Fu, 0x10u, 0x17u})
            QVERIFY(isWriteFunction(fc));
        for (unsigned fc : {0x01u, 0x02u, 0x03u, 0x04u})
            QVERIFY(!isWriteFunction(fc));
    }

    void readLine()
    {
        const unsigned char pdu[] = {0x00, 0x0A, 0x00, 0x0C};
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x03, pdu, 4)),
                 QStringLiteral("[4] unit=1 fc=0x03 read addr=10 count=12"));
    }

    void bitReads()
    {
        const unsigned char pdu[] = {0x00, 0x02, 0x00, 0x05};
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x01, pdu, 4)),
                 QStringLiteral("[4] unit=1 fc=0x01 read addr=2 count=5"));
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x02, pdu, 4)),
                 QStringLiteral("[4] unit=1 fc=0x02 read addr=2 count=5"));
    }

    void coilWrites()
    {
        const unsigned char on[] = {0x00, 0x03, 0xFF, 0x00};
        const unsigned char off[] = {0x00, 0x03, 0x00, 0x00};
        const unsigned char bad[] = {0x00, 0x03, 0x12, 0x34};
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x05, on, 4)),
                 QStringLiteral("[4] unit=1 fc=0x05 write coil addr=3 value=ON"));
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x05, off, 4)),
                 QStringLiteral("[4] unit=1 fc=0x05 write coil addr=3 value=OFF"));
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x05, bad, 4)),
                 QStringLiteral("[4] unit=1 fc=0x05 write coil addr=3 value=0x1234"));

        // 3 coils from address 10: ON, OFF, ON (bit 0 first).
        const unsigned char multi[] = {0x00, 0x0A, 0x00, 0x03, 0x01, 0x05};
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x0F, multi, 6)),
                 QStringLiteral("[4] unit=1 fc=0x0F write coils addr=10 count=3 values=ON,OFF,ON"));
    }

    void coilWriteListIsCapped()
    {
        // 20 coils, all ON: only 16 are listed.
        const unsigned char many[] = {0x00, 0x00, 0x00, 0x14, 0x03, 0xFF, 0xFF, 0x0F};
        const QString line = QString::fromStdString(describeRequest(4, 1, 0x0F, many, 8));
        QVERIFY2(line.endsWith(QStringLiteral("count=20 values=ON,ON,ON,ON,ON,ON,ON,ON,ON,ON,ON,ON,ON,ON,ON,ON,...")),
                 qPrintable(line));
        // A truncated request prints what is there and does not read past the buffer.
        const unsigned char cut[] = {0x00, 0x00, 0x00, 0x14, 0x03};
        const QString cutLine = QString::fromStdString(describeRequest(4, 1, 0x0F, cut, 5));
        QVERIFY2(cutLine.contains(QStringLiteral("count=20")), qPrintable(cutLine));
    }

    void singleWrite()
    {
        const unsigned char pdu[] = {0x00, 0x64, 0x03, 0xE8};
        QCOMPARE(QString::fromStdString(describeRequest(4, 1, 0x06, pdu, 4)),
                 QStringLiteral("[4] unit=1 fc=0x06 write addr=100 value=0x03E8 (1000)"));
    }

    // 123.5f = 0x42F70000: ABCD sends 0x42F7, 0x0000.
    void float32WriteDecodedInEveryOrder()
    {
        const unsigned char pdu[] = {0x00, 0x6E, 0x00, 0x02, 0x04, 0x42, 0xF7, 0x00, 0x00};
        const QString line = QString::fromStdString(describeRequest(4, 1, 0x10, pdu, 9));
        QVERIFY2(line.startsWith(QStringLiteral(
                     "[4] unit=1 fc=0x10 write addr=110 count=2 values=0x42F7,0x0000 | float ABCD=123.5 CDAB=")),
                 qPrintable(line));
        // The same value sent as CDAB decodes as 123.5 under CDAB.
        QVERIFY(QString::fromStdString(describe32(0x0000, 0x42F7)).contains(QStringLiteral("CDAB=123.5 ")));
        // BADC: bytes swapped inside each register.
        QVERIFY(QString::fromStdString(describe32(0xF742, 0x0000)).contains(QStringLiteral("BADC=123.5 ")));
        // DCBA: little endian.
        QVERIFY(QString::fromStdString(describe32(0x0000, 0xF742)).endsWith(QStringLiteral("DCBA=123.5")));
    }

    void longWriteTruncated()
    {
        unsigned char pdu[5 + 2 * 20] = {0x00, 0x00, 0x00, 20, 40};
        const QString line = QString::fromStdString(describeRequest(4, 1, 0x10, pdu, int(sizeof(pdu))));
        QVERIFY(line.endsWith(QStringLiteral(",...")));
        QVERIFY(!line.contains(QStringLiteral("float")));
    }
};

QTEST_MAIN(TstRequestLog)
#include "tst_requestlog.moc"
