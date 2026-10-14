#include "AnalysisPanel.h"

// Stub created by T190; implemented by T194.

namespace mb {

AnalysisPanel::AnalysisPanel(QWidget *parent) : QWidget(parent) {}

AnalysisPanel::~AnalysisPanel() = default;

void AnalysisPanel::setRecorder(const Recorder *recorder)
{
    m_recorder = recorder;
}

void AnalysisPanel::setPlot(TrendPlot *plot)
{
    m_plot = plot;
}

void AnalysisPanel::setTags(const QList<Tag> &tags)
{
    m_tags = tags;
}

void AnalysisPanel::setSettings(const AnalysisSettings &settings)
{
    m_settings = settings;
}

AnalysisSettings AnalysisPanel::settings() const
{
    return m_settings;
}

QList<StepResult> AnalysisPanel::results() const
{
    return m_results;
}

QString AnalysisPanel::readoutText() const
{
    return {};
}

void AnalysisPanel::refresh() {}

void AnalysisPanel::resetData() {}

} // namespace mb
