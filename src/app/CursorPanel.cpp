#include "CursorPanel.h"

// Stub: implemented by its task (see TASKS.md).

namespace mb {

CursorPanel::CursorPanel(QWidget *parent) : QWidget(parent) {}
void CursorPanel::setRecorder(const Recorder *recorder) { m_recorder = recorder; }
void CursorPanel::setTags(const QList<Tag> &tags) { m_tags = tags; }
void CursorPanel::setCursors(std::optional<double> t1, std::optional<double> t2) { m_t1 = t1; m_t2 = t2; }
QString CursorPanel::readoutText() const { return {}; }
void CursorPanel::refresh() {}

} // namespace mb