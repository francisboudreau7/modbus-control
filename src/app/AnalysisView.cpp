#include "AnalysisView.h"

#include "TrendPlot.h"

#include <QHBoxLayout>

// Stub created by T90; implemented by T95.

namespace mb {

AnalysisView::AnalysisView(QWidget *parent)
    : QWidget(parent)
    , m_plot(new TrendPlot(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_plot);
}

AnalysisView::~AnalysisView() = default;

void AnalysisView::setRecorder(const Recorder *recorder)
{
    m_recorder = recorder;
    m_plot->setRecorder(recorder);
}

void AnalysisView::setTags(const QList<Tag> &tags) { m_tags = tags; }
void AnalysisView::setSettings(const AnalysisSettings &settings) { m_settings = settings; }
AnalysisSettings AnalysisView::settings() const { return m_settings; }
TrendPlot *AnalysisView::trendPlot() const { return m_plot; }
QList<StepResult> AnalysisView::results() const { return m_results; }
QString AnalysisView::readoutText() const { return {}; }
void AnalysisView::refresh() { m_plot->refresh(); }
void AnalysisView::resetData() { m_plot->resetData(); }
void AnalysisView::setCursors(std::optional<double>, std::optional<double>) {}

} // namespace mb
