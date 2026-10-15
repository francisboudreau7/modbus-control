#include "LoopWindow.h"

#include "LoopController.h"

#include <QGraphicsView>
#include <QHBoxLayout>
#include <QVBoxLayout>

// Stubs created by T210; implemented by T214.

namespace mbloop {

SchematicView::SchematicView(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    m_view = new QGraphicsView(this);
    layout->addWidget(m_view);
}

void SchematicView::refresh(const LoopController &) {}
void SchematicView::setProcessDataVisible(bool visible) { m_processData = visible; }
bool SchematicView::processDataVisible() const { return m_processData; }
QString SchematicView::labelText(const QString &) const { return {}; }
QGraphicsView *SchematicView::view() const { return m_view; }

Faceplate::Faceplate(LoopController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller)
{
}

void Faceplate::refresh() {}

LoopWindow::LoopWindow(LoopController *controller, QWidget *parent)
    : QMainWindow(parent), m_controller(controller)
{
    setWindowTitle(QStringLiteral("Modbus Loop Simulator \u2014 Level loop 101"));
    auto *central = new QWidget(this);
    auto *row = new QHBoxLayout(central);
    m_schematic = new SchematicView(central);
    m_faceplate = new Faceplate(controller, central);
    row->addWidget(m_schematic, 1);
    row->addWidget(m_faceplate);
    setCentralWidget(central);
}

SchematicView *LoopWindow::schematic() const { return m_schematic; }
Faceplate *LoopWindow::faceplate() const { return m_faceplate; }
void LoopWindow::setServerStatus(const QString &) {}

} // namespace mbloop
