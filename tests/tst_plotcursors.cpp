#include "PlotCursors.h"

#include <QtTest>

#include <qwt_axis.h>
#include <qwt_plot.h>
#include <qwt_plot_item.h>
#include <qwt_plot_marker.h>
#include <qwt_scale_map.h>
#include <qwt_text.h>

#include <QApplication>
#include <QCursor>

using namespace mb;

namespace {
constexpr double kAxisMin = 0.0;
constexpr double kAxisMax = 100.0;
} // namespace

class TstPlotCursors : public QObject
{
    Q_OBJECT

private:
    QwtPlot *m_plot = nullptr;
    PlotCursors *m_cursors = nullptr;

    QWidget *canvas() const { return m_plot->canvas(); }

    // Canvas-local pixel position of plot time t, vertically centred (dx offsets it).
    QPoint at(double t, int dx = 0) const
    {
        const QwtScaleMap map = m_plot->canvasMap(QwtAxis::XBottom);
        return QPoint(qRound(map.transform(t)) + dx, canvas()->height() / 2);
    }

    // One pixel is 100 / canvasWidth units; allow about 1.5 px of rounding.
    double tol() const { return 1.5 * (kAxisMax - kAxisMin) / canvas()->width(); }

    QwtPlotMarker *markerNamed(const QString &name) const
    {
        const QwtPlotItemList items = m_plot->itemList(QwtPlotItem::Rtti_PlotMarker);
        for (QwtPlotItem *item : items) {
            if (item->title().text() == name)
                return static_cast<QwtPlotMarker *>(item);
        }
        return nullptr;
    }

private slots:
    void init()
    {
        m_plot = new QwtPlot;
        m_plot->setAxisScale(QwtAxis::XBottom, kAxisMin, kAxisMax);
        m_plot->setAxisScale(QwtAxis::YLeft, 0.0, 10.0);
        m_plot->resize(520, 320);
        m_plot->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_plot));
        QVERIFY(canvas()->width() > 200);
        m_cursors = new PlotCursors(m_plot); // parented to the plot
    }

    void cleanup()
    {
        delete m_plot; // also deletes m_cursors (child)
        m_plot = nullptr;
        m_cursors = nullptr;
    }

    void placeTwoCursorsByClicks()
    {
        QSignalSpy cursorsSpy(m_cursors, &PlotCursors::cursorsChanged);
        QSignalSpy placingSpy(m_cursors, &PlotCursors::placingChanged);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        QVERIFY(!m_cursors->isPlacing());
        m_cursors->setPlacing(true);
        QVERIFY(m_cursors->isPlacing());
        QCOMPARE(placingSpy.count(), 1);
        QCOMPARE(placingSpy.at(0).at(0).toBool(), true);

        // First click sets C1; placing continues.
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(20));
        QVERIFY(m_cursors->hasC1());
        QVERIFY(!m_cursors->hasC2());
        QVERIFY(m_cursors->isPlacing());
        QVERIFY(qAbs(m_cursors->c1() - 20.0) <= tol());
        QCOMPARE(interactedSpy.count(), 1);
        QCOMPARE(cursorsSpy.count(), 1);
        QCOMPARE(placingSpy.count(), 1);

        // Second click sets C2 and ends placing.
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(70));
        QVERIFY(m_cursors->hasC2());
        QVERIFY(qAbs(m_cursors->c2() - 70.0) <= tol());
        QVERIFY(!m_cursors->isPlacing());
        QCOMPARE(interactedSpy.count(), 2);
        QCOMPARE(cursorsSpy.count(), 2);
        QCOMPARE(placingSpy.count(), 2);
        QCOMPARE(placingSpy.at(1).at(0).toBool(), false);

        // Placing is over: a later click far from both lines does nothing.
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(45));
        QCOMPARE(interactedSpy.count(), 2);
        QCOMPARE(cursorsSpy.count(), 2);
        QVERIFY(qAbs(m_cursors->c1() - 20.0) <= tol());
        QVERIFY(qAbs(m_cursors->c2() - 70.0) <= tol());
    }

    void escCancelsPlacing()
    {
        QSignalSpy placingSpy(m_cursors, &PlotCursors::placingChanged);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        m_cursors->setPlacing(true);
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(30));
        QVERIFY(m_cursors->hasC1());
        QVERIFY(m_cursors->isPlacing());

        QTest::keyClick(canvas(), Qt::Key_Escape);
        QVERIFY(!m_cursors->isPlacing());
        QCOMPARE(placingSpy.count(), 2);
        QCOMPARE(placingSpy.at(1).at(0).toBool(), false);
        QVERIFY(m_cursors->hasC1()); // cancel keeps C1 that was already placed
        QVERIFY(!m_cursors->hasC2());

        // A click after cancelling does not set C2.
        const int before = interactedSpy.count();
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(60));
        QVERIFY(!m_cursors->hasC2());
        QCOMPARE(interactedSpy.count(), before);
        QVERIFY(qAbs(m_cursors->c1() - 30.0) <= tol());

        // Esc while not placing is ignored.
        const int placingCount = placingSpy.count();
        QTest::keyClick(canvas(), Qt::Key_Escape);
        QCOMPARE(placingSpy.count(), placingCount);
    }

    void dragC1MovesIt()
    {
        m_cursors->setC1(20.0);
        m_cursors->setC2(70.0);
        QSignalSpy cursorsSpy(m_cursors, &PlotCursors::cursorsChanged);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        // Press 2 px right of the C1 line (within the 5 px grab zone).
        QTest::mousePress(canvas(), Qt::LeftButton, Qt::NoModifier, at(20.0, 2));
        QCOMPARE(interactedSpy.count(), 1);

        QTest::mouseMove(canvas(), at(40.0));
        QVERIFY(qAbs(m_cursors->c1() - 40.0) <= tol());
        QVERIFY(cursorsSpy.count() >= 1);

        QTest::mouseRelease(canvas(), Qt::LeftButton, Qt::NoModifier, at(40.0));
        QVERIFY(qAbs(m_cursors->c1() - 40.0) <= tol());
        QVERIFY(qAbs(m_cursors->c2() - 70.0) <= tol()); // C2 untouched

        // After release, moving the mouse does not move C1 any more.
        const int countAfterRelease = cursorsSpy.count();
        QTest::mouseMove(canvas(), at(80.0));
        QVERIFY(qAbs(m_cursors->c1() - 40.0) <= tol());
        QCOMPARE(cursorsSpy.count(), countAfterRelease);
    }

    void dragC2AndClampToAxis()
    {
        m_cursors->setC1(20.0);
        m_cursors->setC2(70.0);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        QTest::mousePress(canvas(), Qt::LeftButton, Qt::NoModifier, at(70.0));
        QCOMPARE(interactedSpy.count(), 1);
        QTest::mouseMove(canvas(), at(90.0));
        QTest::mouseRelease(canvas(), Qt::LeftButton, Qt::NoModifier, at(90.0));
        QVERIFY(qAbs(m_cursors->c2() - 90.0) <= tol());
        QVERIFY(qAbs(m_cursors->c1() - 20.0) <= tol());

        // Dragging past the right edge clamps to the axis maximum.
        const QPoint beyond(canvas()->width() + 50, canvas()->height() / 2);
        QTest::mousePress(canvas(), Qt::LeftButton, Qt::NoModifier, at(90.0));
        QTest::mouseMove(canvas(), beyond);
        QTest::mouseRelease(canvas(), Qt::LeftButton, Qt::NoModifier, beyond);
        QVERIFY(qAbs(m_cursors->c2() - kAxisMax) <= tol());
    }

    void pressFarFromLinesDoesNothing()
    {
        m_cursors->setC1(20.0);
        m_cursors->setC2(70.0);
        QSignalSpy cursorsSpy(m_cursors, &PlotCursors::cursorsChanged);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        // 45 is about 25 units (roughly 120 px) away from both lines.
        QTest::mousePress(canvas(), Qt::LeftButton, Qt::NoModifier, at(45.0));
        QTest::mouseMove(canvas(), at(50.0));
        QTest::mouseRelease(canvas(), Qt::LeftButton, Qt::NoModifier, at(50.0));

        QCOMPARE(interactedSpy.count(), 0);
        QCOMPARE(cursorsSpy.count(), 0);
        QVERIFY(qAbs(m_cursors->c1() - 20.0) <= tol());
        QVERIFY(qAbs(m_cursors->c2() - 70.0) <= tol());
        QVERIFY(!m_cursors->isPlacing());
    }

    void rightButtonDoesNothing()
    {
        m_cursors->setC1(20.0);
        QSignalSpy cursorsSpy(m_cursors, &PlotCursors::cursorsChanged);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        // Right press on the C1 line and drag: cursors must not move.
        QTest::mousePress(canvas(), Qt::RightButton, Qt::NoModifier, at(20.0));
        QTest::mouseMove(canvas(), at(40.0));
        QTest::mouseRelease(canvas(), Qt::RightButton, Qt::NoModifier, at(40.0));
        QCOMPARE(interactedSpy.count(), 0);
        QCOMPARE(cursorsSpy.count(), 0);
        QVERIFY(qAbs(m_cursors->c1() - 20.0) <= tol());

        // Right click while placing does not set C1.
        m_cursors->clear();
        m_cursors->setPlacing(true);
        QTest::mouseClick(canvas(), Qt::RightButton, Qt::NoModifier, at(30.0));
        QVERIFY(!m_cursors->hasC1());
        QVERIFY(m_cursors->isPlacing());
        QCOMPARE(interactedSpy.count(), 0);
    }

    void setMarkersVisibleKeepsPositions()
    {
        m_cursors->setC1(20.0);
        m_cursors->setC2(70.0);
        QwtPlotMarker *c1 = markerNamed(QStringLiteral("C1"));
        QwtPlotMarker *c2 = markerNamed(QStringLiteral("C2"));
        QVERIFY(c1 && c2);
        QVERIFY(c1->isVisible());
        QVERIFY(c2->isVisible());

        m_cursors->setMarkersVisible(false);
        QVERIFY(!c1->isVisible());
        QVERIFY(!c2->isVisible());
        QVERIFY(m_cursors->hasC1());
        QVERIFY(m_cursors->hasC2());
        QVERIFY(qAbs(m_cursors->c1() - 20.0) <= tol());
        QVERIFY(qAbs(m_cursors->c2() - 70.0) <= tol());

        // Hidden cursors cannot be grabbed.
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(20.0));
        QCOMPARE(interactedSpy.count(), 0);
        QVERIFY(qAbs(m_cursors->c1() - 20.0) <= tol());

        // Positions are still there when shown again, and grabbing works again.
        m_cursors->setMarkersVisible(true);
        QVERIFY(c1->isVisible());
        QVERIFY(c2->isVisible());
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(20.0));
        QCOMPARE(interactedSpy.count(), 1);
    }

    void markerPlacedWhileHiddenStaysHidden()
    {
        m_cursors->setMarkersVisible(false);
        m_cursors->setC1(30.0);
        QwtPlotMarker *c1 = markerNamed(QStringLiteral("C1"));
        QVERIFY(c1);
        QVERIFY(!c1->isVisible());
        QVERIFY(qAbs(m_cursors->c1() - 30.0) <= tol());
    }

    void clearRemovesCursorsAndPlacing()
    {
        m_cursors->setC1(20.0);
        m_cursors->setC2(70.0);
        m_cursors->setPlacing(true);
        QVERIFY(markerNamed(QStringLiteral("C1")));

        QSignalSpy cursorsSpy(m_cursors, &PlotCursors::cursorsChanged);
        QSignalSpy placingSpy(m_cursors, &PlotCursors::placingChanged);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        m_cursors->clear();
        QVERIFY(!m_cursors->hasC1());
        QVERIFY(!m_cursors->hasC2());
        QVERIFY(!m_cursors->isPlacing());
        QCOMPARE(cursorsSpy.count(), 1);
        QCOMPARE(placingSpy.count(), 1);
        QCOMPARE(placingSpy.at(0).at(0).toBool(), false);

        // Markers are removed from the plot.
        QVERIFY(markerNamed(QStringLiteral("C1")) == nullptr);
        QVERIFY(markerNamed(QStringLiteral("C2")) == nullptr);

        // Nothing is grabbable any more.
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(20.0));
        QCOMPARE(interactedSpy.count(), 0);

        // Clearing again is harmless, and cursors can be placed again.
        m_cursors->clear();
        m_cursors->setPlacing(true);
        QTest::mouseClick(canvas(), Qt::LeftButton, Qt::NoModifier, at(40.0));
        QVERIFY(m_cursors->hasC1());
        QVERIFY(qAbs(m_cursors->c1() - 40.0) <= tol());
        QVERIFY(markerNamed(QStringLiteral("C1")) != nullptr);
    }

    void programmaticSetC1C2()
    {
        QSignalSpy cursorsSpy(m_cursors, &PlotCursors::cursorsChanged);
        QSignalSpy interactedSpy(m_cursors, &PlotCursors::interacted);

        m_cursors->setC1(25.0);
        QVERIFY(m_cursors->hasC1());
        QVERIFY(!m_cursors->hasC2());
        QCOMPARE(m_cursors->c1(), 25.0);
        QCOMPARE(cursorsSpy.count(), 1);
        QVERIFY(markerNamed(QStringLiteral("C1")) != nullptr);
        QVERIFY(markerNamed(QStringLiteral("C1"))->isVisible());

        m_cursors->setC2(75.0);
        QVERIFY(m_cursors->hasC2());
        QCOMPARE(m_cursors->c2(), 75.0);
        QCOMPARE(m_cursors->c1(), 25.0);
        QCOMPARE(cursorsSpy.count(), 2);
        QVERIFY(markerNamed(QStringLiteral("C2")) != nullptr);

        // Moving an existing cursor programmatically keeps the same marker.
        QwtPlotMarker *c1 = markerNamed(QStringLiteral("C1"));
        m_cursors->setC1(10.0);
        QCOMPARE(m_cursors->c1(), 10.0);
        QCOMPARE(markerNamed(QStringLiteral("C1")), c1);
        QCOMPARE(cursorsSpy.count(), 3);

        // Programmatic changes are not user interaction.
        QCOMPARE(interactedSpy.count(), 0);
    }

    void hoverChangesCursorShape()
    {
        m_cursors->setC1(20.0);
        m_cursors->setC2(70.0);
        const Qt::CursorShape base = canvas()->cursor().shape();

        // Hover within 5 px of the C1 line: horizontal resize cursor.
        QTest::mouseMove(canvas(), at(20.0, 3));
        QCOMPARE(canvas()->cursor().shape(), Qt::SizeHorCursor);

        // Hover away from both lines: the resize cursor is dropped.
        QTest::mouseMove(canvas(), at(45.0));
        QVERIFY(canvas()->cursor().shape() != Qt::SizeHorCursor);
        // Known bug: updateHoverCursor() calls unsetCursor(), so the canvas falls back to
        // the widget default (ArrowCursor) instead of the CrossCursor it set itself
        // (QwtPlotAbstractCanvas). Remove this expected failure once that is fixed.
        QEXPECT_FAIL("", "unsetCursor() drops the canvas's own CrossCursor (should restore it)",
                     Continue);
        QCOMPARE(canvas()->cursor().shape(), base);

        // Near C2 as well.
        QTest::mouseMove(canvas(), at(70.0, -2));
        QCOMPARE(canvas()->cursor().shape(), Qt::SizeHorCursor);

        // Leaving the canvas drops the resize cursor too.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(canvas(), &leave);
        QVERIFY(canvas()->cursor().shape() != Qt::SizeHorCursor);
    }
};

QTEST_MAIN(TstPlotCursors)
#include "tst_plotcursors.moc"
