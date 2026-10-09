#include "AppController.h"

#include "MainWindow.h"

// Stub: implemented by its task (see TASKS.md).

namespace mb {

AppController::AppController(MainWindow *window, QObject *parent) : QObject(parent), m_window(window)
{
    m_window->setDocumentTitle(QStringLiteral("Untitled"), false);
}
AppController::~AppController() = default;
bool AppController::openProject(const QString &) { return false; }

} // namespace mb