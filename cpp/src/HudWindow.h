#pragma once

#include "DisplaySnapshot.h"
#include "DockerStatus.h"
#include "JournalStatus.h"
#include "KubeStatus.h"
#include "ServicesStatus.h"
#include "AlertEngine.h"
#include "IncidentStatus.h"
#include "WatchdogStatus.h"
#include "Notifier.h"
#include "SyntheticStatus.h"
#include "TraceStatus.h"
#include "PathStatus.h"
#include "StatsdStatus.h"
#include "DbmStatus.h"
#include "TlsStatus.h"
#include "FimStatus.h"

#include <QPoint>
#include <QRect>
#include <QWidget>

#include <memory>

class QLabel;
class QKeyEvent;
class QLayout;
class QMouseEvent;
class QPushButton;
class DashboardPanel;

class HudWindow : public QWidget {
    Q_OBJECT

public:
    explicit HudWindow(QWidget *parent = nullptr);

    void setSnapshot(const DisplaySnapshot &snapshot);

    void setDockerSnapshot(const DockerSnapshot &snapshot);

    void setKubeSnapshot(const KubeSnapshot &snapshot);

    void setServicesSnapshot(const ServicesSnapshot &snapshot);

    void setJournalSnapshot(const JournalSnapshot &snapshot);

    void setNotifier(std::unique_ptr<Notifier> notifier);

    void setSyntheticSnapshot(const SyntheticSnapshot &snapshot);

    void setTraceSnapshot(const TraceSnapshot &snapshot);

    void setPathSnapshot(const PathSnapshot &snapshot);

    void setStatsdSnapshot(const StatsdSnapshot &snapshot);

    void setDbmSnapshot(const DbmSnapshot &snapshot);

    void setTlsSnapshot(const TlsSnapshot &snapshot);

    void setFimSnapshot(const FimSnapshot &snapshot);

    bool isExpanded() const { return m_detailsExpanded; }

protected:
    void showEvent(QShowEvent *event) override;
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
    QWidget *m_pages = nullptr;
    QWidget *m_compactPage = nullptr;
    QRect m_savedGeometry;
    AlertEngine m_alerts;
    IncidentTracker m_incidents;
    QList<double> m_cpuHistory;
    QList<double> m_hostCpuHistory;
    AlertInput m_alertInput;
    std::unique_ptr<Notifier> m_notifier;
    void evaluateAlerts();
    QPushButton *m_moreButton = nullptr;
    QPushButton *m_showHideButton = nullptr;
    DisplaySnapshot m_lastSnapshot;
    bool m_detailsExpanded = false;
    bool m_compact = false;

    bool m_dragging = false;
    QPoint m_dragOffset;

    QPoint m_buttonPressGlobal;
    bool m_buttonDragPending = false;
    bool m_buttonDragActive = false;
    bool m_buttonManualMove = false;
};
