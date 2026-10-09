#include "AnalysisCsv.h"

#include <QBuffer>
#include <QHash>
#include <QIODevice>
#include <QList>
#include <QString>
#include <QtTest>

#include <limits>
#include <optional>

using namespace mb;

namespace {

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

SetpointResult setpoint(double percent, double target, bool found, double time = kNan,
                        double measured = kNan)
{
    SetpointResult sp;
    sp.percent = percent;
    sp.target = target;
    sp.found = found;
    sp.time = time;
    sp.measured = measured;
    return sp;
}

// Valid result: C1 = 10 s, C2 = 30 s, y0 = 10, y100 = 30, step at 10 s, K = 2, L = 2.35, T = 2.01.
StepResult validLag(TagId id = 1)
{
    StepResult r;
    r.tagId = id;
    r.valid = true;
    r.t1 = 10.0;
    r.t2 = 30.0;
    r.y0 = 10.0;
    r.y100 = 30.0;
    r.stepTime = 10.0;
    r.deltaPv = 20.0;
    r.deltaCo = 10.0;
    r.gain = 2.0;
    r.deadTime = 2.35;
    r.timeConstant = 2.01;
    r.setpoints << setpoint(0.0, 10.0, true, 12.25, 10.02)
                << setpoint(63.2, 22.64, true, 14.5, 22.6);
    return r;
}

QString run(const QList<StepResult> &results, const QHash<TagId, QString> &headers,
            QChar delimiter = u',')
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QString error;
    if (!exportAnalysisCsv(buffer, results, headers, delimiter, &error))
        return QStringLiteral("export failed: ") + error;
    return QString::fromUtf8(buffer.data());
}

} // namespace

class Test : public QObject
{
    Q_OBJECT
private slots:
    void validResult()
    {
        const QHash<TagId, QString> headers{{1, QStringLiteral("Lag [degC]")}};
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "Lag [degC],0,10,10.02,12.25,2.25\r\n"
                           "Lag [degC],63.2,22.64,22.6,14.5,4.5\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n"
                           "Lag [degC],10,30,10,30,20,10,10,2,2.35,2.01,,,\r\n");
        QCOMPARE(run({validLag()}, headers), expected);
    }

    void fitColumnsPresent()
    {
        const QHash<TagId, QString> headers{{1, QStringLiteral("Lag [degC]")}};
        StepResult r = validLag();
        r.fitRms = 0.12;
        r.fitRmsPercent = 0.6;
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "Lag [degC],0,10,10.02,12.25,2.25\r\n"
                           "Lag [degC],63.2,22.64,22.6,14.5,4.5\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n"
                           "Lag [degC],10,30,10,30,20,10,10,2,2.35,2.01,0.12,0.6,\r\n");
        QCOMPARE(run({r}, headers), expected);
    }

    void notFoundSetpoint()
    {
        const QHash<TagId, QString> headers{{1, QStringLiteral("Lag [degC]")}};
        StepResult r = validLag();
        r.setpoints = {setpoint(50.0, 20.0, false)};
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "Lag [degC],50,20,,,\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n"
                           "Lag [degC],10,30,10,30,20,10,10,2,2.35,2.01,,,\r\n");
        QCOMPARE(run({r}, headers), expected);
    }

    void invalidResult()
    {
        const QHash<TagId, QString> headers{{1, QStringLiteral("Lag [degC]")}};
        StepResult r;
        r.tagId = 1;
        r.valid = false;
        r.error = QStringLiteral("No data at C1");
        r.t1 = 10.0;
        r.t2 = 30.0;
        r.setpoints << setpoint(0.0, kNan, false); // must not be written
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n"
                           "Lag [degC],10,30,,,,,,,,,,,No data at C1\r\n");
        QCOMPARE(run({r}, headers), expected);
    }

    void missingTagHeader()
    {
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "Tag 7,0,10,10.02,12.25,2.25\r\n"
                           "Tag 7,63.2,22.64,22.6,14.5,4.5\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n"
                           "Tag 7,10,30,10,30,20,10,10,2,2.35,2.01,,,\r\n");
        QCOMPARE(run({validLag(7)}, {}), expected);
    }

    void headerNeedingQuotes()
    {
        const QHash<TagId, QString> headers{{1, QStringLiteral("Flow, \"A\"")}};
        StepResult r = validLag();
        r.setpoints = {setpoint(0.0, 10.0, true, 12.5, 10.0)};
        r.deltaCo = std::nullopt;
        r.gain = std::nullopt;
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "\"Flow, \"\"A\"\"\",0,10,10,12.5,2.5\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n"
                           "\"Flow, \"\"A\"\"\",10,30,10,30,20,,10,,2.35,2.01,,,\r\n");
        QCOMPARE(run({r}, headers), expected);
    }

    void semicolonDelimiter()
    {
        const QHash<TagId, QString> headers{{1, QStringLiteral("Lag [degC]")}};
        StepResult r = validLag();
        r.setpoints = {setpoint(0.0, 10.0, true, 12.5, 10.0)};
        r.deltaCo = std::nullopt;
        r.gain = std::nullopt;
        const QString expected =
            QStringLiteral("tag;percent;target;measured;time_s;time_from_c1_s\r\n"
                           "Lag [degC];0;10;10;12.5;2.5\r\n"
                           "\r\n"
                           "tag;c1_s;c2_s;y0;y100;delta_pv;delta_co;step_time_s;gain_k;"
                           "dead_time_l_s;time_constant_t_s;fit_rms;fit_rms_percent;note\r\n"
                           "Lag [degC];10;30;10;30;20;;10;;2.35;2.01;;;\r\n");
        QCOMPARE(run({r}, headers, u';'), expected);
    }

    void emptyResults()
    {
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n");
        QCOMPARE(run({}, {}), expected);
    }

    void numberFormatting()
    {
        const QHash<TagId, QString> headers{{3, QStringLiteral("x")}};
        StepResult r;
        r.tagId = 3;
        r.valid = true;
        r.t1 = 0.1;
        r.t2 = 1e-7;
        r.y0 = -2.5;
        r.y100 = 0.1;
        r.stepTime = 0.1;
        r.deltaPv = 2.6;
        r.setpoints << setpoint(0.1, -2.5, true, 0.1, 1e-7);
        const QString expected =
            QStringLiteral("tag,percent,target,measured,time_s,time_from_c1_s\r\n"
                           "x,0.1,-2.5,1e-07,0.1,0\r\n"
                           "\r\n"
                           "tag,c1_s,c2_s,y0,y100,delta_pv,delta_co,step_time_s,gain_k,"
                           "dead_time_l_s,time_constant_t_s,fit_rms,fit_rms_percent,note\r\n"
                           "x,0.1,1e-07,-2.5,0.1,2.6,,0.1,,,,,,\r\n");
        QCOMPARE(run({r}, headers), expected);
    }

    void deviceNotWritable()
    {
        QBuffer buffer; // not opened
        QString error;
        QVERIFY(!exportAnalysisCsv(buffer, {validLag()}, {}, u',', &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(buffer.data().isEmpty());
    }
};

QTEST_MAIN(Test)
#include "tst_analysiscsv.moc"
