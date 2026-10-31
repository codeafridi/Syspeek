#pragma once

#include "DisplaySnapshot.h"
#include "HistoryLog.h"
#include "DockerStatus.h"
#include "JournalStatus.h"
#include "KubeStatus.h"
#include "ProcessActions.h"
#include "ServicesStatus.h"
#include "AlertEngine.h"

#include "Config.h"

#include "IncidentStatus.h"
#include "WatchdogStatus.h"
#include "AlertWebhook.h"
#include "MetricsExport.h"
#include "SyntheticStatus.h"
#include "TraceStatus.h"
#include "PathStatus.h"
#include "StatsdStatus.h"
#include "DbmStatus.h"
#include "TlsStatus.h"
#include "FimStatus.h"
#include "OverlayStatus.h"
#include "TimelineStatus.h"

#include <QHash>
#include <QPoint>
#include <QSet>
#include <QRect>
#include <QWidget>

#include <memory>

class QLabel;
class QKeyEvent;
class QLayout;
class QMouseEvent;
class QMoveEvent;
class QPushButton;
class QResizeEvent;
class QTimer;
class DashboardPanel;
struct SettingsInfo;

class HudWindow : public QWidget {
    Q_OBJECT

public:
    explicit HudWindow(QWidget *parent = nullptr);

    void setSnapshot(const DisplaySnapshot &snapshot);

    void setDockerSnapshot(const DockerSnapshot &snapshot);

    void setKubeSnapshot(const KubeSnapshot &snapshot);

    void setServicesSnapshot(const ServicesSnapshot &snapshot);

    void setJournalSnapshot(const JournalSnapshot &snapshot);

    void setMonitors(const QList<AlertRule> &rules);

    void applyMonitorChange(const AlertRule &rule);
    void resetMonitors();

    // M78 verbs: process actions are applied in-process through the platform
    // seam; service/container verbs are forwarded to their own source
    // objects (they own the supervisor / Engine API sockets).
    void applyProcessAction(const ProcessAction &action);
    void setActionsEnabled(bool enabled);
    void setServiceActionStatus(const QString &text, bool ok);
    void setContainerActionStatus(const QString &text, bool ok);

    // M75 silences: authored quiet windows over single monitors.
    void setSilences(const QList<SilenceRule> &rules);
    void addSilence(const QString &matcher, int minutes);
    void expireSilence(const QString &matcher);

signals:
    void serviceRestartRequested(const QString &unit);
    void containerActionRequested(const QString &id, const QString &verb);

public:

    void setWebhookConfig(const WebhookConfig &config);

    void setExportIntervalMs(int intervalMs);

    void setSyntheticSnapshot(const SyntheticSnapshot &snapshot);

    void setTraceSnapshot(const TraceSnapshot &snapshot);

    void setPathSnapshot(const PathSnapshot &snapshot);

    void setStatsdSnapshot(const StatsdSnapshot &snapshot);

    void setDbmSnapshot(const DbmSnapshot &snapshot);

    void setTlsSnapshot(const TlsSnapshot &snapshot);

    void setFimSnapshot(const FimSnapshot &snapshot);

    void setSettingsInfo(const SettingsInfo &info);
    void setHistoryStore(const QString &path, int capacity);

    // M86: the event stream. One bounded in-memory ring fed by every
    // collector that can say "something changed".
    void setTimelineConfig(bool enabled, int capacity, bool markers,
                           int windowSeconds);
    void focusTimeline(qint64 atMs);
    void noteActionEvent(TimelineKind kind, const QString &subject,
                         const QString &title, const QString &detail, bool ok);
    QList<TimelineEvent> timelineEvents() const { return m_events; }

    // M80: the overlay's own state (session, click-through, pinned, geometry).
    // An empty path disables persistence, which is what tests use.
    void setOverlayConfig(const OverlayConfig &config, const QString &path);
    OverlayState overlayState() const { return m_overlay; }

    bool isExpanded() const { return m_detailsExpanded; }

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLabel *makeCaption(const QString &text);
    QLabel *makeValue();

    QLayout *makeMetricBlock(const QString &caption, QLabel *value);

    void toggleDetails();
    void toggleCompact();
    void toggleDockerDetails();
    void toggleKubeDetails();
    void updateCompactText();

    // M86 event stream.
    void recordEvent(const TimelineEvent &event);
    void publishTimeline();
    bool eventFirstSeen(const QString &key);
    void feedStructuredEvents();
    QList<TimelineEvent> m_events;
    QSet<QString> m_eventSeen;
    QSet<QString> m_activeAlertIds;
    QSet<QString> m_activeIncidentIds;
    QHash<QString, QString> m_checkStates;
    int m_timelineTick = 0;
    bool m_timelineDirty = false;
    int m_eventCapacity = 2000;
    bool m_eventsEnabled = true;
    bool m_eventMarkers = true;
    qint64 m_timelineFocusMs = 0;
    qint64 m_timelineWindowMs = 90000;

    QLabel *m_appValue = nullptr;
    QLabel *m_pidValue = nullptr;
    QLabel *m_cpuValue = nullptr;
    QLabel *m_memoryValue = nullptr;
    QLabel *m_statusValue = nullptr;

    QWidget *m_metricsWidget = nullptr;
    QLabel *m_compactLabel = nullptr;
    QPushButton *m_dockerButton = nullptr;
    QLabel *m_dockerDetails = nullptr;
    QString m_dockerDetailsText;
    bool m_dockerOpen = false;
    QPushButton *m_kubeButton = nullptr;
    QLabel *m_kubeDetails = nullptr;
    QString m_kubeDetailsText;
    bool m_kubeOpen = false;
    DashboardPanel *m_panel = nullptr;
    HistoryLog m_historyLog;
    int m_historyCapacity = 0;
    int m_historySeek = -1;
    int m_historyTick = 0;
    int m_historyWindowCount = -1;
    qint64 m_historySpanMs = 0;
    void publishHistory();
    QWidget *m_pages = nullptr;
    QWidget *m_compactPage = nullptr;
    QRect m_savedGeometry;
    AlertEngine m_alerts;
    IncidentTracker m_incidents;
    QList<SilenceRule> m_silences;
    QList<double> m_cpuHistory;
    QList<double> m_hostCpuHistory;
    QList<double> m_stealHistory;
    AlertInput m_alertInput;
    AlertWebhook *m_webhook = nullptr;
    QList<ExportRow> m_exportRows;
    int m_exportIntervalMs = 1000;
    void exportHistory(const QString &format);
    void seekHistory(int index);
    void evaluateAlerts();
    QPushButton *m_moreButton = nullptr;
    QPushButton *m_showHideButton = nullptr;
    QPushButton *m_clickButton = nullptr;
    QTimer *m_overlaySaveTimer = nullptr;
    OverlaySession m_overlaySession = OverlaySession::Unknown;
    OverlayState m_overlay;
    OverlayConfig m_overlayConfig;
    QString m_overlayPath;
    QRect m_overlayPersisted;
    bool m_overlayRestored = false;
    bool m_overlayAboveRequested = false;
    bool m_overlayStickySupported = false;
    void toggleClickThrough();
    void applyClickThrough(bool on);
    void persistOverlay();
    void restoreOverlay();
    void ensureOnScreen();
    void verifyOverlayStates(int attempt);
    void publishOverlayState();
    void refreshOverlayState();
    DisplaySnapshot m_lastSnapshot;
    bool m_detailsExpanded = false;
    bool m_actionsEnabled = true;
    bool m_compact = false;

    bool m_dragging = false;
    QPoint m_dragOffset;

    QPoint m_buttonPressGlobal;
    bool m_buttonDragPending = false;
    bool m_buttonDragActive = false;
    bool m_buttonManualMove = false;
};
