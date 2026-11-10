#pragma once

// Internal page of AnalysisPanel (not a contract): the closed-loop area. For now a placeholder;
// a later task adds its settings, results table, checks, prediction and overlays.

#include "AnalysisCommon.h"

#include <QWidget>

#include <functional>

class QLabel;
class TstAnalysisPanel;

namespace mb {

/// Page 1 of the panel's "modeStack", object name "closedLoopPage". Children: the QLabel
/// "closedLoopPlaceholder" ("The closed-loop analysis is not available yet.").
///
/// Same interface as OpenLoopPage. The panel owns the Context (settings including
/// AnalysisSettings::closedLoop / controller / spTagId, tags, recorder, plot, the open-loop step
/// results) and calls:
/// - syncFromSettings() after it changed the settings (no user-edit hook call);
/// - recompute() on every panel recompute (cursors, settings, tags, data under the refresh rule);
/// - rebuildOverlays() inside its own overlay rebuild (attach only while the overlays are shown;
///   the panel replots once), clearOverlays() (detach + delete), forgetOverlays() (the plot was
///   destroyed and deleted the items);
/// - setOverlaysShown() when the mode changes (Closed loop shows them);
/// - copyText() / readoutText(): this page's part of "Copy results" and of the image-export
///   readout, added by the panel in Closed loop mode when not empty.
/// The user's edits write Context::settings and call the user-edit hook given to the constructor.
/// Currently it computes and shows nothing.
class ClosedLoopPage : public QWidget
{
    Q_OBJECT
public:
    ClosedLoopPage(analysis::Context *context, std::function<void()> userEdited,
                   QWidget *parent = nullptr);
    ~ClosedLoopPage() override;

    void syncFromSettings();
    void recompute();

    void setOverlaysShown(bool shown);
    bool overlaysShown() const { return m_overlaysShown; }
    void rebuildOverlays();
    void clearOverlays();
    void forgetOverlays();

    /// Empty for now.
    QString copyText() const;
    /// Empty for now.
    QString readoutText() const;

private:
    friend class ::TstAnalysisPanel;

    analysis::Context *m_ctx;
    std::function<void()> m_userEdited;
    QLabel *m_placeholder = nullptr;
    bool m_overlaysShown = false;
};

} // namespace mb
