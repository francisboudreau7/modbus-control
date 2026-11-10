#include "ClosedLoopPage.h"

#include <QLabel>
#include <QVBoxLayout>

namespace mb {

ClosedLoopPage::ClosedLoopPage(analysis::Context *context, std::function<void()> userEdited,
                               QWidget *parent)
    : QWidget(parent), m_ctx(context), m_userEdited(std::move(userEdited))
{
    setObjectName(QStringLiteral("closedLoopPage"));
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    m_placeholder = new QLabel(tr("The closed-loop analysis is not available yet."), this);
    m_placeholder->setObjectName(QStringLiteral("closedLoopPlaceholder"));
    m_placeholder->setWordWrap(true);
    col->addWidget(m_placeholder);
}

ClosedLoopPage::~ClosedLoopPage()
{
    clearOverlays();
}

void ClosedLoopPage::syncFromSettings()
{
}

void ClosedLoopPage::recompute()
{
}

void ClosedLoopPage::setOverlaysShown(bool shown)
{
    if (shown == m_overlaysShown)
        return;
    m_overlaysShown = shown;
    rebuildOverlays();
}

void ClosedLoopPage::rebuildOverlays()
{
    clearOverlays();
}

void ClosedLoopPage::clearOverlays()
{
}

void ClosedLoopPage::forgetOverlays()
{
}

QString ClosedLoopPage::copyText() const
{
    return {};
}

QString ClosedLoopPage::readoutText() const
{
    return {};
}

} // namespace mb
