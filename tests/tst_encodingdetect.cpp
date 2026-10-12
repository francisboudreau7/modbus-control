#include <QtTest>

// Stub created by T150; filled in by its task.

class TstEncodingDetect : public QObject
{
    Q_OBJECT
private slots:
    void placeholder() { QVERIFY(true); }
};

QTEST_MAIN(TstEncodingDetect)
#include "tst_encodingdetect.moc"
