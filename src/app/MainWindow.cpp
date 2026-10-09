#include "MainWindow.h"

#include "ConnectionBar.h"
#include "CursorPanel.h"
#include "TagTableModel.h"
#include "TagTableWidget.h"
#include "TrendPlot.h"

#include <QCloseEvent>

// Stub: implemented by its task (see TASKS.md).

namespace mb {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_connectionBar(new ConnectionBar(this))
    , m_tagModel(new TagTableModel(this))
    , m_tagTable(new TagTableWidget(m_tagModel, this))
    , m_trendPlot(new TrendPlot(this))
    , m_cursorPanel(new CursorPanel(this))
{
    setCentralWidget(m_trendPlot);
    resize(1200, 800);
}
MainWindow::~MainWindow() = default;
ConnectionBar *MainWindow::connectionBar() const { return m_connectionBar; }
TagTableModel *MainWindow::tagModel() const { return m_tagModel; }
TagTableWidget *MainWindow::tagTable() const { return m_tagTable; }
TrendPlot *MainWindow::trendPlot() const { return m_trendPlot; }
CursorPanel *MainWindow::cursorPanel() const { return m_cursorPanel; }
const MainWindow::Actions &MainWindow::actions() const { return m_actions; }
void MainWindow::setConnectionStatus(ConnectionState, const QString &) {}
void MainWindow::showStatusMessage(const QString &, int) {}
void MainWindow::setDocumentTitle(const QString &name, bool modified)
{
    setWindowTitle(name + QStringLiteral("[*] — Modbus Control"));
    setWindowModified(modified);
}
void MainWindow::setCloseHandler(std::function<bool()> handler) { m_closeHandler = std::move(handler); }
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeHandler && !m_closeHandler())
        event->ignore();
    else
        event->accept();
}

} // namespace mb