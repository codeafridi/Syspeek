#include "DashboardPanel.h"

#include "Theme.h"
#include "platform/PlatformFactory.h"

#include "LogParser.h"
#include "MemoryFormat.h"
#include "SensitiveStatus.h"
#include "Sparkline.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace {

QFont panelTitleFont() {
    QFont font;
    font.setPointSize(9);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    return font;
}

QFont monoFont(int size, bool bold = false) {
    QFont font;
    font.setStyleHint(QFont::Monospace);
    font.setFamily(QStringLiteral("monospace"));
    font.setPointSize(size);
    font.setBold(bold);
    return font;
}

QString formatRate(double bytesPerSec) {
    const double kb = bytesPerSec / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB/s").arg(qRound(kb));
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        return QStringLiteral("%1 MB/s").arg(mb, 0, 'f', 1);
    }
    return QStringLiteral("%1 GB/s").arg(mb / 1024.0, 0, 'f', 2);
}

QString statusColor(const QString &status) {
    if (status == QStringLiteral("HIGH CPU")) {
        return ui_theme::crit();
    }
    if (status == QStringLiteral("WARN")) {
        return ui_theme::warn();
    }
    if (status == QStringLiteral("OK")) {
        return ui_theme::ok();
    }
    return ui_theme::dim();
}

QLabel *makeTitle(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("ddCardTitle"));
    label->setFont(panelTitleFont());
    return label;
}

QFrame *makeCard(const QString &title, QWidget *content, QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("ddCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 14);
    layout->setSpacing(10);
    layout->addWidget(makeTitle(title, card));
    if (auto *label = qobject_cast<QLabel *>(content)) {
        label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    }
    layout->addWidget(content, 1);
    return card;
}

QFrame *makeStatCard(const QString &caption, QWidget *content, QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("ddStat"));
    auto *title = makeTitle(caption, card);
    title->setFixedHeight(14);
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 11, 14, 12);
    layout->setSpacing(7);
    layout->addWidget(title);
    layout->addWidget(content, 1);
    if (auto *label = qobject_cast<QLabel *>(content)) {
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return card;
}

} // namespace

QString DashboardPanel::unsupportedOsText() const {
    return QStringLiteral("not supported on this OS (%1)").arg(m_osName);
}

DashboardPanel::DashboardPanel(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("ddRoot"));

    const PlatformCapabilities osCaps = systemBackend().capabilities();
    m_osTcpCounters = osCaps.tcpCounters;
    m_osCgroups = osCaps.cgroups;
    m_osPsi = osCaps.psi;
    m_osJournal = osCaps.journal;
    m_osDpkgSbom = osCaps.sbom;
    m_osFim = osCaps.fim;
    m_osSecurity = osCaps.namespaces;
    m_osName = systemBackend().platformName();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *topbar = new QFrame(this);
    topbar->setObjectName(QStringLiteral("ddTopbar"));
    auto *topLayout = new QHBoxLayout(topbar);
    topLayout->setContentsMargins(16, 10, 12, 10);
    topLayout->setSpacing(10);

    auto *chip = new QLabel(QStringLiteral("SYSPEEK"), topbar);
    chip->setObjectName(QStringLiteral("ddChip"));
    chip->setFont(panelTitleFont());

    m_appValue = new QLabel(QStringLiteral("--"), topbar);
    m_appValue->setObjectName(QStringLiteral("ddApp"));
    m_appValue->setFont(monoFont(13, true));

    m_pidValue = new QLabel(QStringLiteral("pid --"), topbar);
    m_pidValue->setObjectName(QStringLiteral("ddPid"));
    m_pidValue->setFont(monoFont(9));

    m_statusValue = new QLabel(QStringLiteral("IDLE"), topbar);
    m_statusValue->setObjectName(QStringLiteral("ddStatus"));
    m_statusValue->setFont(panelTitleFont());

    auto *collapse = new QPushButton(QStringLiteral("COLLAPSE"), topbar);
    collapse->setObjectName(QStringLiteral("ddCollapse"));
    collapse->setCursor(Qt::PointingHandCursor);
    collapse->setFocusPolicy(Qt::NoFocus);
    connect(collapse, &QPushButton::clicked, this,
            &DashboardPanel::collapseRequested);

    topLayout->addWidget(chip);
    topLayout->addSpacing(4);
    topLayout->addWidget(m_appValue);
    topLayout->addWidget(m_pidValue);
    topLayout->addStretch(1);
    topLayout->addWidget(m_statusValue);
    topLayout->addWidget(collapse);
    root->addWidget(topbar);

    auto *body = new QWidget(this);
    auto *bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_rail = new QListWidget(body);
    m_rail->setObjectName(QStringLiteral("ddRail"));
    m_rail->setFixedWidth(158);
    m_rail->setFocusPolicy(Qt::NoFocus);
    m_rail->addItems({QStringLiteral("Overview"), QStringLiteral("Metrics"),
                      QStringLiteral("Alerts"), QStringLiteral("Incidents"), QStringLiteral("SLO"),
                      QStringLiteral("Processes"), QStringLiteral("Memory"),
                      QStringLiteral("Disks"),
                      QStringLiteral("Host"),
                      QStringLiteral("Network"),
                      QStringLiteral("Network Path"),
                      QStringLiteral("Containers"), QStringLiteral("Kubernetes"),
                      QStringLiteral("Services"), QStringLiteral("Logs"),
                      QStringLiteral("Errors"), QStringLiteral("Dependencies"),
                      QStringLiteral("Software"),
                      QStringLiteral("Endpoints"),
                      QStringLiteral("Synthetic"),
                      QStringLiteral("Instrumentation"),
                      QStringLiteral("Traces"), QStringLiteral("Profiles"),
                      QStringLiteral("Databases"), QStringLiteral("Security"),
                      QStringLiteral("Cost"), QStringLiteral("Settings")});
    m_rail->setCurrentRow(0);

    m_views = new QStackedWidget(body);
    m_views->setObjectName(QStringLiteral("ddViews"));
    m_views->addWidget(buildOverviewView());
    m_views->addWidget(buildMetricsView());
    m_views->addWidget(buildAlertsView());
    m_views->addWidget(buildIncidentsView());
    m_views->addWidget(buildSloView());
    m_views->addWidget(buildProcessesView());
    m_views->addWidget(buildMemoryView());
    m_views->addWidget(buildDisksView());
    m_views->addWidget(buildHostView());
    m_views->addWidget(buildNetworkView());
    m_views->addWidget(buildNetworkPathView());
    m_views->addWidget(buildContainersView());
    m_views->addWidget(buildKubernetesView());
    m_views->addWidget(buildServicesView());
    m_views->addWidget(buildLogsView());
    m_views->addWidget(buildErrorsView());
    m_views->addWidget(buildDependenciesView());
    m_views->addWidget(buildSoftwareView());
    m_views->addWidget(buildEndpointsView());
    m_views->addWidget(buildSyntheticView());
    m_views->addWidget(buildInstrumentationView());
    m_views->addWidget(buildTracesView());
    m_views->addWidget(buildProfilesView());
    m_views->addWidget(buildDatabasesView());
    m_views->addWidget(buildSecurityView());    m_views->addWidget(buildCostView());
    m_views->addWidget(buildSettingsView());

    connect(m_rail, &QListWidget::currentRowChanged, m_views,
            &QStackedWidget::setCurrentIndex);

    bodyLayout->addWidget(m_rail);
    bodyLayout->addWidget(m_views, 1);
    root->addWidget(body, 1);

    setStyleSheet(QStringLiteral(
        "#ddRoot { background-color: %BG%; }"
        "#ddTopbar { background-color: %RAISED%; border-bottom: 1px solid %BORDER%; }"
        "#ddChip { color: %ACCENT%; font-size: 9px; }"
        "#ddApp { color: %TEXT%; font-size: 13px; }"
        "#ddPid { color: %DIM%; font-size: 9px; }"
        "#ddStatus { color: %OK%; background-color: transparent;"
        "  border: 1px solid %OK%; padding: 2px 10px; border-radius: 2px;"
        "  font-size: 8px; }"
        "#ddCollapse { color: %DIM%; background-color: transparent;"
        "  border: 1px solid %BORDER%; border-radius: 2px; padding: 4px 12px;"
        "  font-size: 9px; }"
        "#ddCollapse:hover { color: %TEXT%; border-color: %MUTED%; }"
        "#ddRail { background-color: %BG%; border: none;"
        "  border-right: 1px solid %BORDER%; color: %DIM%; outline: none;"
        "  font-size: 11px; }"
        "#ddRail::item { padding: 7px 14px; border-left: 3px solid transparent; }"
        "#ddRail::item:hover { background-color: %HOVER%; color: %TEXT%; }"
        "#ddRail::item:selected { background-color: %HOVER%; color: %TEXT%;"
        "  border-left: 3px solid %ACCENT%; }"
        "#ddViews { background-color: %BG%; }"
        "#ddViews QWidget { background-color: transparent; }"
        "#ddCard { background-color: %SURFACE%; border: 1px solid %BORDER%;"
        "  border-radius: 3px; }"
        "#ddStat { background-color: %SURFACE%; border: 1px solid %BORDER%;"
        "  border-radius: 3px; }"
        "#ddCardTitle { color: %MUTED%; font-size: 9px; }"
        "#ddCpu, #ddMem, #ddProcCount, #ddStatusBig, #ddHostCpu, #ddHostMem,"
        " #ddHostDisk, #ddHostNet, #ddHostLoad { color: %TEXT%; }"
        "#ddMemDetail { color: %DIM%; }"
        "#ddFocusDetail, #ddOpenFiles { color: %DIM%; }"
        "#ddCgroup { color: %DIM%; }"
        "#ddDocker { color: %DIM%; }"
        "#ddListen, #ddStates, #ddIfaces { color: %DIM%; }"
        "#ddTcpSummary { color: %DIM%; }"
        "#ddTcp { color: %MUTED%; }"
        "#ddSecuritySummary { color: %DIM%; }"
        "#ddSecurity { color: %MUTED%; }"
        "#ddThreats { color: %MUTED%; }"
        "#ddFimSummary { color: %DIM%; }"
        "#ddFim { color: %MUTED%; }"
        "#ddSoftwareSummary { color: %DIM%; }"
        "#ddSoftware { color: %MUTED%; }"
        "#ddPathSummary { color: %DIM%; }"
        "#ddPathTargets { color: %MUTED%; }"
        "#ddStatsdSummary { color: %DIM%; }"
        "#ddStatsd { color: %MUTED%; }"
        "#ddDbmSummary { color: %DIM%; }"
        "#ddDbm { color: %MUTED%; }"
        "#ddDbmStatements { color: %MUTED%; }"
        "#ddWatchdog { color: %DIM%; }"
        "#ddRuntime { color: %MUTED%; }"
        "#ddForecast { color: %MUTED%; }"
        "#ddInsights { color: %MUTED%; }"
        "#ddProfileSummary { color: %DIM%; }"
        "#ddProfileThreads, #ddProfileOffCpu, #ddProfileMemory { color: %MUTED%; }"
        "#ddIncidentsSummary { color: %DIM%; }"
        "#ddIncidents { color: %MUTED%; }"
        "#ddTlsSummary { color: %DIM%; }"
        "#ddTls { color: %MUTED%; }"
        "#ddSensitiveSummary { color: %DIM%; }"
        "#ddSensitive { color: %MUTED%; }"
        "#ddSloSummary { color: %DIM%; }"
        "#ddSlo { color: %MUTED%; }"
        "#ddDeprecations { color: %MUTED%; }"
        "#ddHostInfoSummary { color: %DIM%; }"
        "#ddHostPlatform, #ddHostCpuInfo, #ddHostFilesystems, #ddHostNetInfo { color: %MUTED%; }"
        "#ddServices, #ddServicesSummary, #ddDeps { color: %DIM%; }"
        "#ddServicesChanges { color: %DIM%; }"
        "#ddLogs, #ddLogsSummary, #ddErrors { color: %DIM%; }"
        "#ddEndpoints, #ddInstrumentation, #ddDatabases { color: %DIM%; }"
        "#ddSynthetic, #ddSyntheticSummary { color: %DIM%; }"
        "#ddKubeSummary, #ddKubeEvents { color: %DIM%; }"
        "#ddKubeDiagnosis, #ddKubeTimeline { color: %DIM%; }"
        "#ddSettingsInfo { color: %DIM%; }"
        "#ddSettingsPath { color: %MUTED%; font-size: 9px; }"
        "#ddCopy { color: %DIM%; background-color: transparent;"
        "  border: 1px solid %BORDER%; border-radius: 2px;"
        "  padding: 2px 10px; font-size: 9px; }"
        "#ddCopy:hover { color: %TEXT%; border-color: %MUTED%; }"
        "QTreeWidget { background-color: transparent; border: none;"
        "  color: %DIM%; font-size: 11px; }"
        "QTreeWidget::item { padding: 2px 0px; }"
        "QTreeWidget::item:selected { background-color: %HOVER%;"
        "  color: %TEXT%; }"
        "QTreeWidget::item:hover { background-color: %HOVER%; }"
        "QHeaderView::section { background-color: transparent; color: %MUTED%;"
        "  border: none; border-bottom: 1px solid %BORDER%; padding: 4px 8px;"
        "  font-size: 9px; font-weight: normal; }"
        "QScrollBar:vertical { background: transparent; width: 9px;"
        "  margin: 0; }"
        "QScrollBar::handle:vertical { background-color: %BORDER%;"
        "  border-radius: 2px; min-height: 24px; margin: 1px 2px; }"
        "QScrollBar::handle:vertical:hover { background-color: %MUTED%; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical"
        " { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical"
        " { background: transparent; }"
        "QScrollBar:horizontal { background: transparent; height: 9px;"
        "  margin: 0; }"
        "QScrollBar::handle:horizontal { background-color: %BORDER%;"
        "  border-radius: 2px; min-width: 24px; margin: 2px 1px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal"
        " { width: 0; }"
        "QPushButton { background-color: transparent; }")
        .replace(QStringLiteral("%BG%"), ui_theme::bg())
        .replace(QStringLiteral("%RAISED%"), ui_theme::raised())
        .replace(QStringLiteral("%BORDER%"), ui_theme::border())
        .replace(QStringLiteral("%HOVER%"), ui_theme::hover())
        .replace(QStringLiteral("%TEXT%"), ui_theme::text())
        .replace(QStringLiteral("%DIM%"), ui_theme::dim())
        .replace(QStringLiteral("%MUTED%"), ui_theme::muted())
        .replace(QStringLiteral("%ACCENT%"), ui_theme::accent())
        .replace(QStringLiteral("%OK%"), ui_theme::ok())
        .replace(QStringLiteral("%SURFACE%"), ui_theme::surface()));
}

QWidget *DashboardPanel::buildOverviewView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    m_cpuValue = new QLabel(QStringLiteral("--"), view);
    m_cpuValue->setObjectName(QStringLiteral("ddCpu"));
    m_cpuValue->setFont(monoFont(18, true));
    m_memValue = new QLabel(QStringLiteral("--"), view);
    m_memValue->setObjectName(QStringLiteral("ddMem"));
    m_memValue->setFont(monoFont(18, true));
    m_procCount = new QLabel(QStringLiteral("0"), view);
    m_procCount->setObjectName(QStringLiteral("ddProcCount"));
    m_procCount->setFont(monoFont(18, true));
    m_statusBig = new QLabel(QStringLiteral("IDLE"), view);
    m_statusBig->setObjectName(QStringLiteral("ddStatusBig"));
    m_statusBig->setFont(monoFont(18, true));

    m_cpuSpark = new Sparkline(view);
    m_cpuSpark->setObjectName(QStringLiteral("ddCpuSpark"));
    m_memSpark = new Sparkline(view);
    m_memSpark->setObjectName(QStringLiteral("ddMemSpark"));

    auto *cpuContent = new QWidget(view);
    auto *cpuLayout = new QVBoxLayout(cpuContent);
    cpuLayout->setContentsMargins(0, 0, 0, 0);
    cpuLayout->setSpacing(4);
    cpuLayout->addWidget(m_cpuValue);
    cpuLayout->addWidget(m_cpuSpark);

    auto *memContent = new QWidget(view);
    auto *memLayout = new QVBoxLayout(memContent);
    memLayout->setContentsMargins(0, 0, 0, 0);
    memLayout->setSpacing(4);
    memLayout->addWidget(m_memValue);
    memLayout->addWidget(m_memSpark);

    auto *stats = new QHBoxLayout();
    stats->setSpacing(14);
    stats->addWidget(makeStatCard(QStringLiteral("CPU"), cpuContent, view), 1);
    stats->addWidget(makeStatCard(QStringLiteral("MEMORY (RSS)"), memContent, view), 1);
    stats->addWidget(makeStatCard(QStringLiteral("PROCESSES"), m_procCount, view), 1);
    stats->addWidget(makeStatCard(QStringLiteral("STATUS"), m_statusBig, view), 1);
    layout->addLayout(stats);

    m_hostCpu = new QLabel(QStringLiteral("--"), view);
    m_hostCpu->setObjectName(QStringLiteral("ddHostCpu"));
    m_hostCpu->setFont(monoFont(14, true));
    m_hostMem = new QLabel(QStringLiteral("--"), view);
    m_hostMem->setObjectName(QStringLiteral("ddHostMem"));
    m_hostMem->setFont(monoFont(14, true));
    m_hostDisk = new QLabel(QStringLiteral("--"), view);
    m_hostDisk->setObjectName(QStringLiteral("ddHostDisk"));
    m_hostDisk->setFont(monoFont(14, true));
    m_hostNet = new QLabel(QStringLiteral("--"), view);
    m_hostNet->setObjectName(QStringLiteral("ddHostNet"));
    m_hostNet->setFont(monoFont(14, true));
    m_hostLoad = new QLabel(QStringLiteral("--"), view);
    m_hostLoad->setObjectName(QStringLiteral("ddHostLoad"));
    m_hostLoad->setFont(monoFont(14, true));

    m_hostCpuSpark = new Sparkline(view);
    m_hostCpuSpark->setObjectName(QStringLiteral("ddHostCpuSpark"));
    auto *hostCpuContent = new QWidget(view);
    auto *hostCpuLayout = new QVBoxLayout(hostCpuContent);
    hostCpuLayout->setContentsMargins(0, 0, 0, 0);
    hostCpuLayout->setSpacing(4);
    hostCpuLayout->addWidget(m_hostCpu);
    hostCpuLayout->addWidget(m_hostCpuSpark);

    auto *hostRow = new QHBoxLayout();
    hostRow->setSpacing(14);
    hostRow->addWidget(makeStatCard(QStringLiteral("HOST CPU"), hostCpuContent, view), 1);
    hostRow->addWidget(makeStatCard(QStringLiteral("MEM AVAILABLE"), m_hostMem, view), 1);
    hostRow->addWidget(makeStatCard(QStringLiteral("DISK FREE"), m_hostDisk, view), 1);
    hostRow->addWidget(makeStatCard(QStringLiteral("NET rx / tx"), m_hostNet, view), 1);
    hostRow->addWidget(makeStatCard(QStringLiteral("LOAD 1m"), m_hostLoad, view), 1);
    layout->addLayout(hostRow);

    m_watchdog = new QLabel(QStringLiteral("--"), view);
    m_watchdog->setObjectName(QStringLiteral("ddWatchdog"));
    m_watchdog->setFont(monoFont(10));
    m_watchdog->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("WATCHDOG (anomaly detection)"), m_watchdog, view), 1);

    m_runtime = new QLabel(QStringLiteral("--"), view);
    m_runtime->setObjectName(QStringLiteral("ddRuntime"));
    m_runtime->setFont(monoFont(10));
    m_runtime->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("RUNTIME (SSI)"), m_runtime, view), 1);

    m_forecast = new QLabel(QStringLiteral("--"), view);
    m_forecast->setObjectName(QStringLiteral("ddForecast"));
    m_forecast->setFont(monoFont(10));
    m_forecast->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("FORECAST (predictive)"), m_forecast, view), 1);

    m_insights = new QLabel(QStringLiteral("--"), view);
    m_insights->setObjectName(QStringLiteral("ddInsights"));
    m_insights->setFont(monoFont(10));
    m_insights->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("WATCHDOG INSIGHTS"), m_insights, view), 1);
    layout->addStretch(1);
    return view;
}

QWidget *DashboardPanel::buildMetricsView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_statsdSummary = new QLabel(QStringLiteral("--"), view);
    m_statsdSummary->setObjectName(QStringLiteral("ddStatsdSummary"));
    m_statsdSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("DOGSTATSD (127.0.0.1:8125)"), m_statsdSummary, view));

    m_statsd = new QLabel(QStringLiteral("--"), view);
    m_statsd->setObjectName(QStringLiteral("ddStatsd"));
    m_statsd->setFont(monoFont(10));
    m_statsd->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("METRICS"), m_statsd, view), 1);

    return view;
}

QWidget *DashboardPanel::buildSloView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_sloSummary = new QLabel(QStringLiteral("--"), view);
    m_sloSummary->setObjectName(QStringLiteral("ddSloSummary"));
    m_sloSummary->setFont(monoFont(14, true));
    layout->addWidget(
        makeCard(QStringLiteral("SERVICE LEVEL OBJECTIVE"), m_sloSummary, view));

    m_slo = new QLabel(QStringLiteral("--"), view);
    m_slo->setObjectName(QStringLiteral("ddSlo"));
    m_slo->setFont(monoFont(11));
    m_slo->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("ERROR BUDGET / BURN RATE"), m_slo, view), 1);

    return view;
}

QWidget *DashboardPanel::buildAlertsView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    m_alerts = new QLabel(QStringLiteral("no active alerts"), view);
    m_alerts->setObjectName(QStringLiteral("ddAlerts"));
    m_alerts->setFont(monoFont(11));
    m_alerts->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("ACTIVE ALERTS"), m_alerts, view), 1);
    return view;
}

QWidget *DashboardPanel::buildIncidentsView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_incidentsSummary = new QLabel(QStringLiteral("--"), view);
    m_incidentsSummary->setObjectName(QStringLiteral("ddIncidentsSummary"));
    m_incidentsSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("INCIDENTS"), m_incidentsSummary, view));

    m_incidents = new QLabel(QStringLiteral("no incidents"), view);
    m_incidents->setObjectName(QStringLiteral("ddIncidents"));
    m_incidents->setFont(monoFont(10));
    m_incidents->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("TIMELINE"), m_incidents, view), 1);
    return view;
}

QWidget *DashboardPanel::buildProfilesView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_profileSummary = new QLabel(QStringLiteral("--"), view);
    m_profileSummary->setObjectName(QStringLiteral("ddProfileSummary"));
    m_profileSummary->setFont(monoFont(13, true));
    layout->addWidget(makeCard(
        QStringLiteral("PROFILES (threads / off-CPU / memory)"),
        m_profileSummary, view));

    m_profileThreads = new QLabel(QStringLiteral("--"), view);
    m_profileThreads->setObjectName(QStringLiteral("ddProfileThreads"));
    m_profileThreads->setFont(monoFont(10));
    m_profileThreads->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("THREADS (cpu)"), m_profileThreads, view), 1);

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_profileOffCpu = new QLabel(QStringLiteral("--"), view);
    m_profileOffCpu->setObjectName(QStringLiteral("ddProfileOffCpu"));
    m_profileOffCpu->setFont(monoFont(10));
    m_profileOffCpu->setWordWrap(false);
    row->addWidget(
        makeCard(QStringLiteral("OFF-CPU / WAIT"), m_profileOffCpu, view), 1);
    m_profileMemory = new QLabel(QStringLiteral("--"), view);
    m_profileMemory->setObjectName(QStringLiteral("ddProfileMemory"));
    m_profileMemory->setFont(monoFont(10));
    m_profileMemory->setWordWrap(false);
    row->addWidget(
        makeCard(QStringLiteral("MEMORY (PSS)"), m_profileMemory, view), 1);
    layout->addLayout(row, 1);
    return view;
}

QWidget *DashboardPanel::buildProcessesView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);

    m_procs = new QTreeWidget(view);
    m_procs->setObjectName(QStringLiteral("ddProcs"));
    m_procs->setColumnCount(6);
    m_procs->setHeaderLabels({QStringLiteral("Process"), QStringLiteral("PID"),
                              QStringLiteral("CPU%"), QStringLiteral("Threads"),
                              QStringLiteral("FDs"), QStringLiteral("RSS")});
    m_procs->setRootIsDecorated(true);
    m_procs->setSelectionMode(QAbstractItemView::NoSelection);
    m_procs->setFocusPolicy(Qt::NoFocus);
    m_procs->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 6; ++i) {
        m_procs->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    connect(m_procs, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) {
                if (!m_populating) {
                    m_collapsedPids.remove(item->data(0, Qt::UserRole).toInt());
                }
            });
    connect(m_procs, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) {
                if (!m_populating) {
                    m_collapsedPids.insert(item->data(0, Qt::UserRole).toInt());
                }
            });
    layout->addWidget(makeCard(QStringLiteral("PROCESSES"), m_procs, view), 2);

    auto *detailRow = new QHBoxLayout();
    detailRow->setSpacing(14);

    m_focusDetail = new QLabel(QStringLiteral("--"), view);
    m_focusDetail->setObjectName(QStringLiteral("ddFocusDetail"));
    m_focusDetail->setFont(monoFont(11));
    m_focusDetail->setWordWrap(true);
    detailRow->addWidget(
        makeCard(QStringLiteral("FOCUSED PROCESS"), m_focusDetail, view), 1);

    m_openFiles = new QLabel(QStringLiteral("--"), view);
    m_openFiles->setObjectName(QStringLiteral("ddOpenFiles"));
    m_openFiles->setFont(monoFont(10));
    m_openFiles->setWordWrap(false);
    detailRow->addWidget(
        makeCard(QStringLiteral("OPEN FILES"), m_openFiles, view), 1);

    layout->addLayout(detailRow);
    return view;
}

QWidget *DashboardPanel::buildMemoryView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);

    m_memDetail = new QLabel(QStringLiteral("--"), view);
    m_memDetail->setObjectName(QStringLiteral("ddMemDetail"));
    m_memDetail->setFont(monoFont(12));
    layout->addWidget(makeCard(QStringLiteral("MEMORY BREAKDOWN"), m_memDetail, view));
    layout->addStretch(1);
    return view;
}

QWidget *DashboardPanel::buildNetworkView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    m_net = new QLabel(QStringLiteral("--"), view);
    m_net->setObjectName(QStringLiteral("ddNet"));
    m_net->setFont(monoFont(11));
    layout->addWidget(
        makeCard(QStringLiteral("CONNECTIONS (focused app)"), m_net, view), 2);

    auto *row = new QHBoxLayout();
    row->setSpacing(14);
    m_listen = new QLabel(QStringLiteral("--"), view);
    m_listen->setObjectName(QStringLiteral("ddListen"));
    m_listen->setFont(monoFont(10));
    row->addWidget(makeCard(QStringLiteral("LISTENING ENDPOINTS"), m_listen, view), 1);

    m_states = new QLabel(QStringLiteral("--"), view);
    m_states->setObjectName(QStringLiteral("ddStates"));
    m_states->setFont(monoFont(10));
    row->addWidget(makeCard(QStringLiteral("SOCKET STATES"), m_states, view), 1);
    layout->addLayout(row, 1);

    m_ifaces = new QLabel(QStringLiteral("--"), view);
    m_ifaces->setObjectName(QStringLiteral("ddIfaces"));
    m_ifaces->setFont(monoFont(10));
    layout->addWidget(makeCard(QStringLiteral("INTERFACES"), m_ifaces, view), 1);

    auto *tcpBox = new QWidget(view);
    auto *tcpLayout = new QVBoxLayout(tcpBox);
    tcpLayout->setContentsMargins(0, 0, 0, 0);
    tcpLayout->setSpacing(4);
    m_tcpSummary = new QLabel(QStringLiteral("--"), tcpBox);
    m_tcpSummary->setObjectName(QStringLiteral("ddTcpSummary"));
    m_tcpSummary->setFont(monoFont(11, true));
    m_tcp = new QLabel(QStringLiteral("--"), tcpBox);
    m_tcp->setObjectName(QStringLiteral("ddTcp"));
    m_tcp->setFont(monoFont(10));
    m_tcp->setWordWrap(false);
    tcpLayout->addWidget(m_tcpSummary);
    tcpLayout->addWidget(m_tcp);
    layout->addWidget(makeCard(QStringLiteral("TCP HEALTH"), tcpBox, view), 1);

    auto *tlsBox = new QWidget(view);
    auto *tlsLayout = new QVBoxLayout(tlsBox);
    tlsLayout->setContentsMargins(0, 0, 0, 0);
    tlsLayout->setSpacing(4);
    m_tlsSummary = new QLabel(QStringLiteral("--"), tlsBox);
    m_tlsSummary->setObjectName(QStringLiteral("ddTlsSummary"));
    m_tlsSummary->setFont(monoFont(11, true));
    m_tls = new QLabel(QStringLiteral("--"), tlsBox);
    m_tls->setObjectName(QStringLiteral("ddTls"));
    m_tls->setFont(monoFont(10));
    m_tls->setWordWrap(false);
    tlsLayout->addWidget(m_tlsSummary);
    tlsLayout->addWidget(m_tls);
    layout->addWidget(makeCard(QStringLiteral("TLS CERTIFICATES"), tlsBox, view), 1);
    return view;
}

QWidget *DashboardPanel::buildNetworkPathView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_pathSummary = new QLabel(QStringLiteral("--"), view);
    m_pathSummary->setObjectName(QStringLiteral("ddPathSummary"));
    m_pathSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("NETWORK PATH (mtr)"), m_pathSummary, view));

    m_pathTargets = new QLabel(QStringLiteral("--"), view);
    m_pathTargets->setObjectName(QStringLiteral("ddPathTargets"));
    m_pathTargets->setFont(monoFont(10));
    m_pathTargets->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("TARGETS"), m_pathTargets, view), 1);

    m_pathHops = new QTreeWidget(view);
    m_pathHops->setObjectName(QStringLiteral("ddPathHops"));
    m_pathHops->setColumnCount(9);
    m_pathHops->setHeaderLabels(
        {QStringLiteral("#"), QStringLiteral("Host"), QStringLiteral("Loss%"),
         QStringLiteral("Snt"), QStringLiteral("Last"), QStringLiteral("Avg"),
         QStringLiteral("Best"), QStringLiteral("Wrst"), QStringLiteral("StDev")});
    m_pathHops->setRootIsDecorated(false);
    m_pathHops->setSelectionMode(QAbstractItemView::NoSelection);
    m_pathHops->setFocusPolicy(Qt::NoFocus);
    m_pathHops->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int i = 0; i < 9; ++i) {
        if (i != 1) {
            m_pathHops->header()->setSectionResizeMode(i,
                                                       QHeaderView::ResizeToContents);
        }
    }
    layout->addWidget(makeCard(QStringLiteral("HOPS"), m_pathHops, view), 1);
    return view;
}

QWidget *DashboardPanel::buildContainersView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    m_cgroup = new QLabel(QStringLiteral("--"), view);
    m_cgroup->setObjectName(QStringLiteral("ddCgroup"));
    m_cgroup->setFont(monoFont(11));
    m_cgroup->setWordWrap(true);
    layout->addWidget(makeCard(QStringLiteral("CGROUP"), m_cgroup, view));

    m_docker = new QLabel(QStringLiteral("--"), view);
    m_docker->setObjectName(QStringLiteral("ddDocker"));
    m_docker->setFont(monoFont(11));
    m_docker->setWordWrap(true);
    layout->addWidget(makeCard(QStringLiteral("DOCKER"), m_docker, view));
    layout->addStretch(1);
    return view;
}

QWidget *DashboardPanel::buildKubernetesView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    m_kubeSummary = new QLabel(QStringLiteral("--"), view);
    m_kubeSummary->setObjectName(QStringLiteral("ddKubeSummary"));
    m_kubeSummary->setFont(monoFont(12, true));
    layout->addWidget(makeCard(QStringLiteral("KUBERNETES"), m_kubeSummary, view));

    auto *tables = new QHBoxLayout();
    tables->setSpacing(14);

    m_kubePods = new QTreeWidget(view);
    m_kubePods->setObjectName(QStringLiteral("ddKubePods"));
    m_kubePods->setColumnCount(4);
    m_kubePods->setHeaderLabels({QStringLiteral("Pod"), QStringLiteral("Namespace"),
                                 QStringLiteral("Phase"), QStringLiteral("Restarts")});
    m_kubePods->setRootIsDecorated(false);
    m_kubePods->setSelectionMode(QAbstractItemView::NoSelection);
    m_kubePods->setFocusPolicy(Qt::NoFocus);
    m_kubePods->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 4; ++i) {
        m_kubePods->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    tables->addWidget(makeCard(QStringLiteral("PODS"), m_kubePods, view), 1);

    m_kubeDeploys = new QTreeWidget(view);
    m_kubeDeploys->setObjectName(QStringLiteral("ddKubeDeploys"));
    m_kubeDeploys->setColumnCount(3);
    m_kubeDeploys->setHeaderLabels({QStringLiteral("Deployment"),
                                    QStringLiteral("Namespace"),
                                    QStringLiteral("Ready")});
    m_kubeDeploys->setRootIsDecorated(false);
    m_kubeDeploys->setSelectionMode(QAbstractItemView::NoSelection);
    m_kubeDeploys->setFocusPolicy(Qt::NoFocus);
    m_kubeDeploys->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 3; ++i) {
        m_kubeDeploys->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    tables->addWidget(makeCard(QStringLiteral("DEPLOYMENTS"), m_kubeDeploys, view), 1);
    layout->addLayout(tables, 1);

    m_nodes = new QLabel(QStringLiteral("--"), view);
    m_nodes->setObjectName(QStringLiteral("ddNodes"));
    m_nodes->setFont(monoFont(10));
    m_nodes->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("NODES"), m_nodes, view), 1);

    m_kubeEvents = new QLabel(QStringLiteral("--"), view);
    m_kubeEvents->setObjectName(QStringLiteral("ddKubeEvents"));
    m_kubeEvents->setFont(monoFont(10));
    m_kubeEvents->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("EVENTS (warnings)"), m_kubeEvents, view), 1);

    auto *insight = new QHBoxLayout();
    insight->setSpacing(14);
    m_kubeDiagnosis = new QLabel(QStringLiteral("--"), view);
    m_kubeDiagnosis->setObjectName(QStringLiteral("ddKubeDiagnosis"));
    m_kubeDiagnosis->setFont(monoFont(10));
    m_kubeDiagnosis->setWordWrap(false);
    insight->addWidget(
        makeCard(QStringLiteral("DIAGNOSIS (why broken)"), m_kubeDiagnosis, view), 1);

    m_kubeTimeline = new QLabel(QStringLiteral("--"), view);
    m_kubeTimeline->setObjectName(QStringLiteral("ddKubeTimeline"));
    m_kubeTimeline->setFont(monoFont(10));
    m_kubeTimeline->setWordWrap(false);
    insight->addWidget(
        makeCard(QStringLiteral("CHANGE TIMELINE"), m_kubeTimeline, view), 1);

    m_deprecations = new QLabel(QStringLiteral("--"), view);
    m_deprecations->setObjectName(QStringLiteral("ddDeprecations"));
    m_deprecations->setFont(monoFont(10));
    m_deprecations->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("DEPRECATED / REMOVED APIs"), m_deprecations, view), 1);
    layout->addLayout(insight, 1);
    return view;
}

QWidget *DashboardPanel::buildServicesView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    m_servicesSummary = new QLabel(QStringLiteral("--"), view);
    m_servicesSummary->setObjectName(QStringLiteral("ddServicesSummary"));
    m_servicesSummary->setFont(monoFont(12, true));
    layout->addWidget(makeCard(QStringLiteral("SYSTEMD"), m_servicesSummary, view));

    m_services = new QLabel(QStringLiteral("--"), view);
    m_services->setObjectName(QStringLiteral("ddServices"));
    m_services->setFont(monoFont(10));
    m_services->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("SERVICES"), m_services, view), 1);

    m_servicesChanges = new QLabel(QStringLiteral("--"), view);
    m_servicesChanges->setObjectName(QStringLiteral("ddServicesChanges"));
    m_servicesChanges->setFont(monoFont(10));
    m_servicesChanges->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("CHANGES (restarts)"), m_servicesChanges, view), 1);
    return view;
}

QWidget *DashboardPanel::buildDependenciesView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);

    m_deps = new QLabel(QStringLiteral("--"), view);
    m_deps->setObjectName(QStringLiteral("ddDeps"));
    m_deps->setFont(monoFont(10));
    m_deps->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("SHARED LIBRARIES (focused process)"), m_deps, view),
        1);
    return view;
}

QWidget *DashboardPanel::buildEndpointsView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    m_endpoints = new QLabel(QStringLiteral("--"), view);
    m_endpoints->setObjectName(QStringLiteral("ddEndpoints"));
    m_endpoints->setFont(monoFont(10));
    m_endpoints->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("LISTENING ENDPOINTS (host)"), m_endpoints, view), 1);
    return view;
}

QWidget *DashboardPanel::buildSyntheticView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);

    m_syntheticSummary = new QLabel(QStringLiteral("--"), view);
    m_syntheticSummary->setObjectName(QStringLiteral("ddSyntheticSummary"));
    m_syntheticSummary->setFont(monoFont(12, true));
    layout->addWidget(makeCard(QStringLiteral("SYNTHETIC"), m_syntheticSummary, view));

    m_synthetic = new QLabel(QStringLiteral("--"), view);
    m_synthetic->setObjectName(QStringLiteral("ddSynthetic"));
    m_synthetic->setFont(monoFont(10));
    m_synthetic->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("HTTP CHECKS"), m_synthetic, view), 1);
    return view;
}

QWidget *DashboardPanel::buildInstrumentationView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    m_instrumentation = new QLabel(QStringLiteral("--"), view);
    m_instrumentation->setObjectName(QStringLiteral("ddInstrumentation"));
    m_instrumentation->setFont(monoFont(11));
    layout->addWidget(
        makeCard(QStringLiteral("INSTRUMENTATION (focused app)"), m_instrumentation,
                 view),
        1);
    return view;
}

QWidget *DashboardPanel::buildDatabasesView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_dbmSummary = new QLabel(QStringLiteral("--"), view);
    m_dbmSummary->setObjectName(QStringLiteral("ddDbmSummary"));
    m_dbmSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("DATABASE MONITORING (PostgreSQL)"), m_dbmSummary, view));

    m_dbm = new QLabel(QStringLiteral("--"), view);
    m_dbm->setObjectName(QStringLiteral("ddDbm"));
    m_dbm->setFont(monoFont(10));
    m_dbm->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("DBM"), m_dbm, view), 1);

    m_dbmStatements = new QLabel(QStringLiteral("--"), view);
    m_dbmStatements->setObjectName(QStringLiteral("ddDbmStatements"));
    m_dbmStatements->setFont(monoFont(10));
    m_dbmStatements->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("TOP QUERIES (pg_stat_statements)"), m_dbmStatements, view), 1);

    m_databases = new QLabel(QStringLiteral("--"), view);
    m_databases->setObjectName(QStringLiteral("ddDatabases"));
    m_databases->setFont(monoFont(10));
    m_databases->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("APP DATABASE CONNECTIONS"), m_databases, view), 1);
    return view;
}

QWidget *DashboardPanel::buildLogsView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(14);

    m_logsSummary = new QLabel(QStringLiteral("--"), view);
    m_logsSummary->setObjectName(QStringLiteral("ddLogsSummary"));
    m_logsSummary->setFont(monoFont(12, true));
    layout->addWidget(makeCard(QStringLiteral("JOURNALD"), m_logsSummary, view));

    m_logPipeline = new QLabel(QStringLiteral("--"), view);
    m_logPipeline->setObjectName(QStringLiteral("ddLogPipeline"));
    m_logPipeline->setFont(monoFont(10));
    m_logPipeline->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("LOG PIPELINE (parsed fields)"), m_logPipeline, view), 1);

    auto *sensBox = new QWidget(view);
    auto *sensLayout = new QVBoxLayout(sensBox);
    sensLayout->setContentsMargins(0, 0, 0, 0);
    sensLayout->setSpacing(4);
    m_sensitiveSummary = new QLabel(QStringLiteral("--"), sensBox);
    m_sensitiveSummary->setObjectName(QStringLiteral("ddSensitiveSummary"));
    m_sensitiveSummary->setFont(monoFont(11, true));
    m_sensitive = new QLabel(QStringLiteral("--"), sensBox);
    m_sensitive->setObjectName(QStringLiteral("ddSensitive"));
    m_sensitive->setFont(monoFont(10));
    m_sensitive->setWordWrap(false);
    sensLayout->addWidget(m_sensitiveSummary);
    sensLayout->addWidget(m_sensitive);
    layout->addWidget(
        makeCard(QStringLiteral("SENSITIVE DATA"), sensBox, view), 1);

    m_logs = new QLabel(QStringLiteral("--"), view);
    m_logs->setObjectName(QStringLiteral("ddLogs"));
    m_logs->setFont(monoFont(10));
    m_logs->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("RECENT LOGS"), m_logs, view), 1);
    return view;
}

QWidget *DashboardPanel::buildErrorsView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);

    m_errors = new QLabel(QStringLiteral("--"), view);
    m_errors->setObjectName(QStringLiteral("ddErrors"));
    m_errors->setFont(monoFont(10));
    m_errors->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("ERRORS AND WORSE"), m_errors, view), 1);
    return view;
}

QWidget *DashboardPanel::buildSettingsView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_settingsSoftware = new QLabel(QStringLiteral("--"), view);
    m_settingsSoftware->setObjectName(QStringLiteral("ddSettingsInfo"));
    m_settingsSoftware->setFont(monoFont(10));
    m_settingsSoftware->setTextFormat(Qt::RichText);
    layout->addWidget(makeCard(QStringLiteral("SOFTWARE"), m_settingsSoftware, view));

    m_settingsGeneral = new QLabel(QStringLiteral("--"), view);
    m_settingsGeneral->setObjectName(QStringLiteral("ddSettingsInfo"));
    m_settingsGeneral->setFont(monoFont(10));
    m_settingsGeneral->setTextFormat(Qt::RichText);
    layout->addWidget(makeCard(QStringLiteral("GENERAL"), m_settingsGeneral, view));

    // Diagnostics: command rows with copy buttons. Mirrors the shipped
    // Datadog agent's local surface (status/configcheck/flare per its
    // cmd/agent/subcommands and the embedded CLI strings in its binary;
    // launch-gui "starts the Datadog Agent GUI") mapped onto Syspeek's
    // dump/probe/log/service verbs.
    auto *diagCard = new QFrame(this);
    diagCard->setObjectName(QStringLiteral("ddCard"));
    auto *diagLayout = new QVBoxLayout(diagCard);
    diagLayout->setContentsMargins(14, 12, 14, 14);
    diagLayout->setSpacing(10);
    diagLayout->addWidget(makeTitle(QStringLiteral("DIAGNOSTICS"), diagCard));

    m_settingsDiagnostics = new QLabel(QStringLiteral("--"), view);
    m_settingsDiagnostics->setObjectName(QStringLiteral("ddSettingsPath"));
    m_settingsDiagnostics->setFont(monoFont(9));
    diagLayout->addWidget(m_settingsDiagnostics);

    const QList<QPair<QString, QString>> commands = {
        {QStringLiteral("machine snapshot (JSON)"),
         QStringLiteral("syspeek --dump")},
        {QStringLiteral("pipeline probe (headless tick log)"),
         QStringLiteral("syspeek --probe")},
        {QStringLiteral("tail the HUD log"),
         QStringLiteral("syspeek logs")},
        {QStringLiteral("instance + autostart state"),
         QStringLiteral("syspeek status")},
        {QStringLiteral("stop every HUD instance"),
         QStringLiteral("syspeek stop")},
        {QStringLiteral("restart the HUD"),
         QStringLiteral("syspeek restart")},
    };
    for (const auto &pair : commands) {
        auto *row = new QHBoxLayout();
        row->setSpacing(8);
        auto *what = new QLabel(pair.first, diagCard);
        what->setObjectName(QStringLiteral("ddSettingsInfo"));
        what->setFont(monoFont(10));
        auto *cmd = new QLabel(pair.second, diagCard);
        cmd->setObjectName(QStringLiteral("ddSettingsPath"));
        cmd->setFont(monoFont(9));
        auto *copy = new QPushButton(QStringLiteral("COPY"), diagCard);
        copy->setObjectName(QStringLiteral("ddCopy"));
        copy->setCursor(Qt::PointingHandCursor);
        copy->setFocusPolicy(Qt::NoFocus);
        copy->setToolTip(QStringLiteral("copy '%1'").arg(pair.second));
        copy->setProperty("command", pair.second);
        connect(copy, &QPushButton::clicked, this,
                &DashboardPanel::copySettingCommand);
        row->addWidget(what, 1);
        row->addWidget(cmd);
        row->addWidget(copy);
        diagLayout->addLayout(row);
    }
    layout->addWidget(diagCard);
    layout->addStretch(1);

    applySettingsInfo();
    return view;
}

void DashboardPanel::applySettingsInfo() {
    if (m_settingsSoftware == nullptr) {
        return;
    }
    const QString version = m_settings.version.isEmpty()
        ? QStringLiteral("--")
        : m_settings.version;
    const QString verbatimPlatform = m_osName;
    const QString configStatus = m_settings.configError.isEmpty()
        ? QStringLiteral("ok")
        : QStringLiteral("error: %1").arg(m_settings.configError);
    QString software;
    software += QStringLiteral("<span style=\"color:%1;\">")
        .arg(ui_theme::text());
    software += QStringLiteral("version %1 on %2<br/>")
        .arg(version, verbatimPlatform);
    software += QStringLiteral("</span>");
    software += QStringLiteral("config&nbsp;&nbsp;%1&nbsp;&nbsp;%2<br/>")
        .arg(m_settings.configPath.isEmpty()
                 ? QStringLiteral("--")
                 : m_settings.configPath,
             configStatus);
    software += QStringLiteral("log&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;%1")
        .arg(m_settings.logPath.isEmpty()
                 ? QStringLiteral("--")
                 : m_settings.logPath);
    m_settingsSoftware->setText(software);

    QString general;
    general += QStringLiteral("refresh&nbsp;&nbsp;&nbsp;%1 ms<br/>")
        .arg(m_settings.sampleIntervalMs);
    general += QStringLiteral(
        "alerts&nbsp;&nbsp;&nbsp;&nbsp;"
        "cpu warn %1% - high %2% - rss %3 GB<br/>")
        .arg(QString::number(m_settings.cpuWarnPercent, 'f', 0),
             QString::number(m_settings.cpuHighPercent, 'f', 0),
             QString::number(m_settings.rssWarnGb, 'f', 1));
    general += QStringLiteral("autostart&nbsp;%1<br/>")
        .arg(m_settings.autostartLine.isEmpty()
                 ? QStringLiteral("--")
                 : m_settings.autostartLine);
    general += QStringLiteral("collector&nbsp;local only - reads this "
                              "machine's own %1 interfaces; no telemetry")
        .arg(verbatimPlatform);
    m_settingsGeneral->setText(general);

    QString diagnostics;
    diagnostics += QStringLiteral("log file&nbsp;&nbsp;%1<br/>")
        .arg(m_settings.logPath.isEmpty()
                 ? QStringLiteral("--")
                 : m_settings.logPath);
    diagnostics += QStringLiteral("verbs&nbsp;&nbsp;&nbsp;&nbsp;%1")
        .arg(m_settings.verbsLine.isEmpty()
                 ? QStringLiteral("start stop restart status uninstall")
                 : m_settings.verbsLine);
    m_settingsDiagnostics->setText(diagnostics);
}

void DashboardPanel::setSettingsInfo(const SettingsInfo &info) {
    m_settings = info;
    applySettingsInfo();
}

void DashboardPanel::copySettingCommand() {
    auto *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr) {
        return;
    }
    const QString command = button->property("command").toString();
    if (!command.isEmpty()) {
        QApplication::clipboard()->setText(command);
    }
}

QWidget *DashboardPanel::buildCostView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_costSummary = new QLabel(QStringLiteral("--"), view);
    m_costSummary->setObjectName(QStringLiteral("ddCostSummary"));
    m_costSummary->setFont(monoFont(14, true));
    layout->addWidget(
        makeCard(QStringLiteral("MONTHLY ESTIMATE"), m_costSummary, view));

    m_costWarnings = new QLabel(QStringLiteral("--"), view);
    m_costWarnings->setObjectName(QStringLiteral("ddCostWarnings"));
    m_costWarnings->setFont(monoFont(10));
    m_costWarnings->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("RIGHT-SIZING"), m_costWarnings, view));

    auto *tables = new QHBoxLayout;
    tables->setSpacing(10);

    m_costNamespaces = new QTreeWidget(view);
    m_costNamespaces->setObjectName(QStringLiteral("ddCostNamespaces"));
    m_costNamespaces->setColumnCount(2);
    m_costNamespaces->setHeaderLabels(
        {QStringLiteral("Namespace"), QStringLiteral("$/month")});
    m_costNamespaces->setRootIsDecorated(false);
    m_costNamespaces->setSelectionMode(QAbstractItemView::NoSelection);
    m_costNamespaces->setFocusPolicy(Qt::NoFocus);
    m_costNamespaces->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_costNamespaces->header()->setSectionResizeMode(1,
                                                      QHeaderView::ResizeToContents);
    tables->addWidget(
        makeCard(QStringLiteral("BY NAMESPACE"), m_costNamespaces, view), 1);

    m_costWorkloads = new QTreeWidget(view);
    m_costWorkloads->setObjectName(QStringLiteral("ddCostWorkloads"));
    m_costWorkloads->setColumnCount(2);
    m_costWorkloads->setHeaderLabels(
        {QStringLiteral("Workload"), QStringLiteral("$/month")});
    m_costWorkloads->setRootIsDecorated(false);
    m_costWorkloads->setSelectionMode(QAbstractItemView::NoSelection);
    m_costWorkloads->setFocusPolicy(Qt::NoFocus);
    m_costWorkloads->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_costWorkloads->header()->setSectionResizeMode(1,
                                                    QHeaderView::ResizeToContents);
    tables->addWidget(
        makeCard(QStringLiteral("TOP WORKLOADS"), m_costWorkloads, view), 1);

    layout->addLayout(tables, 1);
    return view;
}

QWidget *DashboardPanel::buildDisksView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_diskSummary = new QLabel(QStringLiteral("--"), view);
    m_diskSummary->setObjectName(QStringLiteral("ddDiskSummary"));
    m_diskSummary->setFont(monoFont(13, true));
    layout->addWidget(makeCard(QStringLiteral("DISK I/O"), m_diskSummary, view));

    m_diskPressure = new QLabel(QStringLiteral("--"), view);
    m_diskPressure->setObjectName(QStringLiteral("ddDiskPressure"));
    m_diskPressure->setFont(monoFont(10));
    m_diskPressure->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("I/O PRESSURE (PSI)"), m_diskPressure, view));

    m_disks = new QTreeWidget(view);
    m_disks->setObjectName(QStringLiteral("ddDisks"));
    m_disks->setColumnCount(7);
    m_disks->setHeaderLabels({QStringLiteral("Device"), QStringLiteral("r/s"),
                              QStringLiteral("w/s"), QStringLiteral("rkB/s"),
                              QStringLiteral("wkB/s"), QStringLiteral("await"),
                              QStringLiteral("%util")});
    m_disks->setRootIsDecorated(false);
    m_disks->setSelectionMode(QAbstractItemView::NoSelection);
    m_disks->setFocusPolicy(Qt::NoFocus);
    m_disks->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 7; ++i) {
        m_disks->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    layout->addWidget(makeCard(QStringLiteral("DEVICES"), m_disks, view), 1);

    return view;
}

QWidget *DashboardPanel::buildHostView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_hostInfoSummary = new QLabel(QStringLiteral("--"), view);
    m_hostInfoSummary->setObjectName(QStringLiteral("ddHostInfoSummary"));
    m_hostInfoSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("HOST INVENTORY"), m_hostInfoSummary, view));

    auto *row1 = new QHBoxLayout;
    row1->setSpacing(10);
    m_hostPlatform = new QLabel(QStringLiteral("--"), view);
    m_hostPlatform->setObjectName(QStringLiteral("ddHostPlatform"));
    m_hostPlatform->setFont(monoFont(10));
    m_hostPlatform->setWordWrap(false);
    row1->addWidget(makeCard(QStringLiteral("PLATFORM"), m_hostPlatform, view), 1);
    m_hostCpuInfo = new QLabel(QStringLiteral("--"), view);
    m_hostCpuInfo->setObjectName(QStringLiteral("ddHostCpuInfo"));
    m_hostCpuInfo->setFont(monoFont(10));
    m_hostCpuInfo->setWordWrap(false);
    row1->addWidget(makeCard(QStringLiteral("CPU"), m_hostCpuInfo, view), 1);
    layout->addLayout(row1, 1);

    auto *row2 = new QHBoxLayout;
    row2->setSpacing(10);
    m_hostFilesystems = new QLabel(QStringLiteral("--"), view);
    m_hostFilesystems->setObjectName(QStringLiteral("ddHostFilesystems"));
    m_hostFilesystems->setFont(monoFont(10));
    m_hostFilesystems->setWordWrap(false);
    row2->addWidget(
        makeCard(QStringLiteral("FILESYSTEMS"), m_hostFilesystems, view), 1);
    m_hostNetwork = new QLabel(QStringLiteral("--"), view);
    m_hostNetwork->setObjectName(QStringLiteral("ddHostNetInfo"));
    m_hostNetwork->setFont(monoFont(10));
    m_hostNetwork->setWordWrap(false);
    row2->addWidget(makeCard(QStringLiteral("NETWORK"), m_hostNetwork, view), 1);
    layout->addLayout(row2, 1);
    return view;
}

QWidget *DashboardPanel::buildSecurityView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_securitySummary = new QLabel(QStringLiteral("--"), view);
    m_securitySummary->setObjectName(QStringLiteral("ddSecuritySummary"));
    m_securitySummary->setFont(monoFont(14, true));
    layout->addWidget(
        makeCard(QStringLiteral("SANDBOX EXPOSURE"), m_securitySummary, view));

    m_security = new QLabel(QStringLiteral("--"), view);
    m_security->setObjectName(QStringLiteral("ddSecurity"));
    m_security->setFont(monoFont(10));
    m_security->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("HARDENING FINDINGS"), m_security, view), 1);

    m_threats = new QLabel(QStringLiteral("--"), view);
    m_threats->setObjectName(QStringLiteral("ddThreats"));
    m_threats->setFont(monoFont(10));
    m_threats->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("THREATS (log-based SIEM)"), m_threats, view), 1);

    m_fimSummary = new QLabel(QStringLiteral("--"), view);
    m_fimSummary->setObjectName(QStringLiteral("ddFimSummary"));
    m_fimSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("FILE INTEGRITY (FIM)"), m_fimSummary, view));

    m_fim = new QLabel(QStringLiteral("--"), view);
    m_fim->setObjectName(QStringLiteral("ddFim"));
    m_fim->setFont(monoFont(10));
    m_fim->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("FILE CHANGES"), m_fim, view), 1);

    return view;
}

QWidget *DashboardPanel::buildTracesView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_traceSummary = new QLabel(QStringLiteral("--"), view);
    m_traceSummary->setObjectName(QStringLiteral("ddTraceSummary"));
    m_traceSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("APM (OTLP :4318)"), m_traceSummary, view));

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_traceMap = new QLabel(QStringLiteral("--"), view);
    m_traceMap->setObjectName(QStringLiteral("ddTraceMap"));
    m_traceMap->setFont(monoFont(10));
    m_traceMap->setWordWrap(false);
    row->addWidget(makeCard(QStringLiteral("SERVICE MAP"), m_traceMap, view), 1);

    m_traceServices = new QLabel(QStringLiteral("--"), view);
    m_traceServices->setObjectName(QStringLiteral("ddTraceServices"));
    m_traceServices->setFont(monoFont(10));
    m_traceServices->setWordWrap(false);
    row->addWidget(makeCard(QStringLiteral("SERVICE LATENCY"), m_traceServices, view), 1);
    layout->addLayout(row, 1);

    m_traceRecent = new QLabel(QStringLiteral("--"), view);
    m_traceRecent->setObjectName(QStringLiteral("ddTraceRecent"));
    m_traceRecent->setFont(monoFont(10));
    m_traceRecent->setWordWrap(false);
    layout->addWidget(makeCard(QStringLiteral("RECENT TRACES"), m_traceRecent, view), 1);

    return view;
}

QWidget *DashboardPanel::buildSoftwareView() {
    auto *view = new QWidget(this);
    auto *layout = new QVBoxLayout(view);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    m_softwareSummary = new QLabel(QStringLiteral("--"), view);
    m_softwareSummary->setObjectName(QStringLiteral("ddSoftwareSummary"));
    m_softwareSummary->setFont(monoFont(13, true));
    layout->addWidget(
        makeCard(QStringLiteral("SBOM (focused app)"), m_softwareSummary, view));

    m_software = new QLabel(QStringLiteral("--"), view);
    m_software->setObjectName(QStringLiteral("ddSoftware"));
    m_software->setFont(monoFont(10));
    m_software->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("PACKAGES"), m_software, view), 1);

    return view;
}

void DashboardPanel::setSnapshot(const DisplaySnapshot &snapshot) {
    m_appValue->setText(snapshot.appName);
    m_pidValue->setText(QStringLiteral("pid %1").arg(snapshot.pid));
    m_cpuValue->setText(snapshot.cpu);
    m_memValue->setText(snapshot.memory);
    m_procCount->setText(QString::number(snapshot.processes.size()));

    m_statusValue->setText(snapshot.status);
    if (m_statusBig != nullptr) {
        m_statusBig->setText(snapshot.status);
        m_statusBig->setStyleSheet(
            QStringLiteral("color:%1;").arg(statusColor(snapshot.status)));
    }
    m_statusValue->setStyleSheet(
        QStringLiteral("color:%1; background-color:#23262b; padding:3px 10px;"
                       " border-radius:3px;")
            .arg(statusColor(snapshot.status)));

    if (snapshot.cpuValid) {
        m_cpuStore.push(snapshot.cpuPercent);
        m_cpuSpark->setValues(m_cpuStore.values());
    }
    if (snapshot.rssKb > 0) {
        m_memStore.push(static_cast<double>(snapshot.rssKb));
        m_memSpark->setValues(m_memStore.values());
    }

    m_memDetail->setText(QStringLiteral("RSS      %1\nVirtual  %2\nSwap     %3\nShared   %4")
                             .arg(formatMemoryKb(snapshot.rssKb),
                                  formatMemoryKb(snapshot.vmsKb),
                                  formatMemoryKb(snapshot.swapKb),
                                  formatMemoryKb(snapshot.sharedKb)));

    QString net;
    int shown = 0;
    for (const ConnectionRow &conn : snapshot.connections) {
        if (shown >= 200) {
            break;
        }
        net += QStringLiteral("%1 %2 -> %3  %4\n")
                   .arg(conn.proto, conn.local, conn.remote, conn.state);
        ++shown;
    }
    m_net->setText(net.isEmpty() ? QStringLiteral("no connections") : net);

    QString endpointText;
    int epCount = 0;
    for (const EndpointRow &entry : snapshot.endpoints) {
        if (epCount >= 40) {
            endpointText += QStringLiteral("...\n");
            break;
        }
        QString line = QStringLiteral("%1 %2:%3  %4")
                           .arg(entry.proto, entry.addr)
                           .arg(entry.port)
                           .arg(entry.exposure);
        if (!entry.service.isEmpty()) {
            line += QStringLiteral("  [%1]").arg(entry.service);
        }
        endpointText += line + QLatin1Char('\n');
        ++epCount;
    }
    m_endpoints->setText(endpointText.isEmpty() ? QStringLiteral("none")
                                                : endpointText);

    m_instrumentation->setText(
        snapshot.instrumentation.isEmpty()
            ? QStringLiteral("no agent detected")
            : snapshot.instrumentation.join(QStringLiteral(", ")));

    QString dbText;
    for (const EndpointRow &entry : snapshot.endpoints) {
        if (!entry.database) {
            continue;
        }
        dbText += QStringLiteral("listen %1 %2:%3 (%4)\n")
                      .arg(entry.proto, entry.addr)
                      .arg(entry.port)
                      .arg(entry.service);
    }
    for (const QString &connection : snapshot.dbConnections) {
        dbText += QStringLiteral("conn   %1\n").arg(connection);
    }
    m_databases->setText(dbText.isEmpty()
                             ? QStringLiteral("no database activity")
                             : dbText);

    QString listen;
    int listenCount = 0;
    for (const QString &endpoint : snapshot.listeningPorts) {
        if (listenCount >= 20) {
            listen += QStringLiteral("...\n");
            break;
        }
        listen += endpoint + QLatin1Char('\n');
        ++listenCount;
    }
    m_listen->setText(listen.isEmpty() ? QStringLiteral("none") : listen);

    QString states;
    for (const QPair<QString, int> &pair : snapshot.socketStateCounts) {
        states += QStringLiteral("%1  %2\n").arg(pair.first, -12).arg(pair.second);
    }
    m_states->setText(states.isEmpty() ? QStringLiteral("none") : states);

    QString ifaces;
    for (const InterfaceRow &row : snapshot.interfaces) {
        ifaces += QStringLiteral("%1 rx %2  tx %3\n")
                      .arg(row.name, -10)
                      .arg(formatRate(row.rxPerSec), formatRate(row.txPerSec));
    }
    m_ifaces->setText(ifaces.isEmpty() ? QStringLiteral("--") : ifaces);

    if (snapshot.tcpHealthAvailable) {
        m_tcpSummary->setText(tcpHealthSummaryText(snapshot.tcpHealth));
        m_tcp->setText(tcpHealthDetailText(snapshot.tcpHealth));
    } else if (!m_osTcpCounters) {
        m_tcpSummary->setText(unsupportedOsText());
        m_tcp->setText(unsupportedOsText());
    } else {
        m_tcpSummary->setText(QStringLiteral("warming up"));
        m_tcp->setText(QStringLiteral("--"));
    }

    if (snapshot.cgroupAvailable) {
        const QString leaf = snapshot.cgroupPath.section(QLatin1Char('/'), -1);
        const QString limit =
            snapshot.cgroupMemLimit < 0
                ? QStringLiteral("unlimited")
                : formatMemoryKb(
                      static_cast<unsigned long long>(snapshot.cgroupMemLimit) / 1024);
        QString text = QStringLiteral("cgroup %1  %2\n")
                           .arg(snapshot.cgroupVersion, leaf);
        text += QStringLiteral("memory   %1 / %2\n")
                    .arg(formatMemoryKb(snapshot.cgroupMemUsage / 1024), limit);
        text += QStringLiteral("anon %1  file %2  kernel %3\n")
                    .arg(formatMemoryKb(snapshot.cgroupAnon / 1024),
                         formatMemoryKb(snapshot.cgroupFile / 1024),
                         formatMemoryKb(snapshot.cgroupKernel / 1024));
        text += QStringLiteral("cpu throttled %1 / %2 periods")
                    .arg(snapshot.cgroupThrottled)
                    .arg(snapshot.cgroupPeriods);
        m_cgroup->setText(text);
    } else if (!m_osCgroups) {
        m_cgroup->setText(unsupportedOsText());
    } else {
        m_cgroup->setText(QStringLiteral("no cgroup stats"));
    }

    if (snapshot.hostAvailable) {
        m_hostCpu->setText(
            snapshot.hostWarming
                ? QStringLiteral("...")
                : QStringLiteral("%1%").arg(qRound(snapshot.hostCpuPercent)));
        m_hostMem->setText(formatMemoryKb(snapshot.hostMemAvailableKb));
        m_hostDisk->setText(formatMemoryKb(snapshot.hostDiskFreeBytes / 1024));
        m_hostNet->setText(QStringLiteral("%1 / %2")
                               .arg(formatRate(snapshot.hostNetRxPerSec),
                                    formatRate(snapshot.hostNetTxPerSec)));
        m_hostLoad->setText(QString::number(snapshot.hostLoad1, 'f', 2));
        if (!snapshot.hostWarming) {
            m_hostCpuStore.push(snapshot.hostCpuPercent);
            m_hostCpuSpark->setValues(m_hostCpuStore.values());
        }
    } else {
        for (QLabel *label :
             {m_hostCpu, m_hostMem, m_hostDisk, m_hostNet, m_hostLoad}) {
            label->setText(QStringLiteral("--"));
        }
    }

    QList<WatchdogMetric> watchdog;
    watchdog.append(WatchdogMetric{
        QStringLiteral("cpu"),
        detectAnomaly(m_cpuStore.values(), AnomalyModel::Mad, 3.5)});
    watchdog.append(WatchdogMetric{
        QStringLiteral("rss"),
        detectAnomaly(m_memStore.values(), AnomalyModel::PredictLinear, 3.0)});
    watchdog.append(WatchdogMetric{
        QStringLiteral("host-cpu"),
        detectAnomaly(m_hostCpuStore.values(), AnomalyModel::ZScore, 3.0)});
    m_watchdog->setText(watchdogText(watchdog));

    m_runtime->setText(runtimeSummaryText(snapshot.runtime)
                       + QStringLiteral("\n")
                       + runtimeDetailText(snapshot.runtime));

    ForecastResult cpuForecast =
        forecastCrossing(m_cpuStore.values(), 90.0, 60);
    cpuForecast.name = QStringLiteral("cpu");
    ForecastResult hostForecast =
        forecastCrossing(m_hostCpuStore.values(), 90.0, 60);
    hostForecast.name = QStringLiteral("host-cpu");
    m_forecast->setText(forecastText(cpuForecast, hostForecast));
    m_insights->setText(
        watchdogInsightsText(watchdog, {cpuForecast, hostForecast}));

    m_profileSummary->setText(profileSummaryText(snapshot.profile));
    m_profileThreads->setText(profileThreadsText(snapshot.profile));
    m_profileOffCpu->setText(profileOffCpuText(snapshot.profile));
    m_profileMemory->setText(profileMemoryText(snapshot.profile));

    DiskSnapshot disk;
    disk.disks = snapshot.disks;
    disk.pressure = snapshot.ioPressure;
    disk.intervalMs = snapshot.diskIntervalMs;
    disk.available = snapshot.disksAvailable;
    disk.warming = snapshot.disksWarming;
    m_diskSummary->setText(diskSummaryText(disk));

    if (snapshot.ioPressureAvailable) {
        m_diskPressure->setText(
            QStringLiteral("io some %1 / %2 / %3   full %4 / %5 / %6")
                .arg(snapshot.ioPressure.someAvg10, 0, 'f', 2)
                .arg(snapshot.ioPressure.someAvg60, 0, 'f', 2)
                .arg(snapshot.ioPressure.someAvg300, 0, 'f', 2)
                .arg(snapshot.ioPressure.fullAvg10, 0, 'f', 2)
                .arg(snapshot.ioPressure.fullAvg60, 0, 'f', 2)
                .arg(snapshot.ioPressure.fullAvg300, 0, 'f', 2));
    } else if (!m_osPsi) {
        m_diskPressure->setText(unsupportedOsText());
    } else {
        m_diskPressure->setText(QStringLiteral("not available"));
    }

    m_disks->clear();
    for (const DiskRow &row : snapshot.disks) {
        auto *item = new QTreeWidgetItem();
        item->setText(0, row.name);
        item->setText(1, QString::number(row.readPerSec, 'f', 1));
        item->setText(2, QString::number(row.writePerSec, 'f', 1));
        item->setText(3, QString::number(row.readKbPerSec, 'f', 0));
        item->setText(4, QString::number(row.writeKbPerSec, 'f', 0));
        item->setText(5, QString::number(row.readAwaitMs, 'f', 1));
        item->setText(6, QString::number(row.utilPercent, 'f', 1));
        for (int col = 1; col < 7; ++col) {
            item->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
        }
        if (row.utilPercent >= 80.0) {
            item->setForeground(0, QColor(QStringLiteral("#d1685f")));
        } else if (row.utilPercent >= 50.0) {
            item->setForeground(0, QColor(QStringLiteral("#d8b25f")));
        }
        m_disks->addTopLevelItem(item);
    }

    m_hostInfoSummary->setText(hostInfoSummaryText(snapshot.hostInfo));
    m_hostPlatform->setText(hostInfoPlatformText(snapshot.hostInfo));
    m_hostCpuInfo->setText(hostInfoCpuText(snapshot.hostInfo));
    m_hostFilesystems->setText(hostInfoFilesystemsText(snapshot.hostInfo));
    m_hostNetwork->setText(hostInfoNetworkText(snapshot.hostInfo));

    if (snapshot.security.available) {
        m_securitySummary->setText(securitySummaryText(snapshot.security));
        m_security->setText(securityDetailText(snapshot.security));
    } else if (!m_osSecurity) {
        m_securitySummary->setText(unsupportedOsText());
        m_security->setText(unsupportedOsText());
    } else {
        m_securitySummary->setText(QStringLiteral("--"));
        m_security->setText(QStringLiteral("--"));
    }

    m_softwareSummary->setText(sbomSummaryText(snapshot.sbom));
    m_software->setText(snapshot.sbom.available
                            ? sbomDetailText(snapshot.sbom)
                            : (m_osDpkgSbom ? QStringLiteral("--")
                                            : unsupportedOsText()));

    m_sloSummary->setText(sloSummaryText(snapshot.slo));
    m_slo->setText(sloDetailText(snapshot.slo));

    m_focusDetail->setText(
        QStringLiteral("threads  %1\nfds      %2\nctx sw   %3 vol / %4 invol\n"
                       "io read  %5\nio write %6")
            .arg(snapshot.focusThreads)
            .arg(snapshot.focusFds < 0 ? QStringLiteral("--")
                                       : QString::number(snapshot.focusFds))
            .arg(snapshot.focusVolCtx)
            .arg(snapshot.focusInvolCtx)
            .arg(formatRate(snapshot.focusIoReadPerSec))
            .arg(formatRate(snapshot.focusIoWritePerSec)));

    QString files;
    int fileCount = 0;
    for (const QString &target : snapshot.focusOpenFiles) {
        if (fileCount >= 12) {
            files += QStringLiteral("...\n");
            break;
        }
        files += target + QStringLiteral("\n");
        ++fileCount;
    }
    m_openFiles->setText(files.isEmpty() ? QStringLiteral("--") : files);

    QString deps;
    int depCount = 0;
    for (const QString &lib : snapshot.dependencies) {
        if (depCount >= 60) {
            deps += QStringLiteral("...\n");
            break;
        }
        deps += lib + QLatin1Char('\n');
        ++depCount;
    }
    m_deps->setText(deps.isEmpty() ? QStringLiteral("--") : deps);

    m_populating = true;
    m_procs->clear();
    QList<QTreeWidgetItem *> byDepth;
    QList<QTreeWidgetItem *> allItems;
    QSet<int> present;
    for (const ProcessRow &row : snapshot.processes) {
        auto *item = new QTreeWidgetItem();
        item->setData(0, Qt::UserRole, row.pid);
        item->setText(0, row.name.isEmpty() ? QStringLiteral("pid %1").arg(row.pid)
                                            : row.name);
        item->setText(1, QString::number(row.pid));
        item->setText(2, row.cpuPercent > 0.0
                             ? QStringLiteral("%1%").arg(row.cpuPercent, 0, 'f', 1)
                             : QStringLiteral("--"));
        item->setText(3, row.threads > 0 ? QString::number(row.threads)
                                         : QStringLiteral("--"));
        item->setText(4, row.fds < 0 ? QStringLiteral("--")
                                     : QString::number(row.fds));
        item->setText(5, row.memory);
        for (int c = 1; c < 6; ++c) {
            item->setTextAlignment(c, Qt::AlignRight | Qt::AlignVCenter);
        }

        allItems.append(item);
        present.insert(row.pid);

        if (row.depth <= 0 || byDepth.size() < row.depth) {
            m_procs->addTopLevelItem(item);
        } else {
            byDepth.at(row.depth - 1)->addChild(item);
        }
        if (byDepth.size() > row.depth) {
            byDepth.resize(row.depth);
        }
        byDepth.append(item);
    }

    m_collapsedPids.intersect(present);
    for (QTreeWidgetItem *item : allItems) {
        item->setExpanded(
            !m_collapsedPids.contains(item->data(0, Qt::UserRole).toInt()));
    }
    m_populating = false;
}

void DashboardPanel::setDockerSnapshot(const DockerSnapshot &snapshot) {
    const QString summary = dockerSummaryText(snapshot);
    const QString details = dockerDetailText(snapshot);
    QString text = summary;
    if (!details.isEmpty() && details != QStringLiteral("no issues")) {
        text += QStringLiteral("\n") + details;
    }
    m_docker->setText(text.isEmpty() ? QStringLiteral("--") : text);
}

void DashboardPanel::setKubeSnapshot(const KubeSnapshot &snapshot) {
    const QString summary = kubeSummaryText(snapshot);
    m_kubeSummary->setText(summary.isEmpty() ? QStringLiteral("--") : summary);

    m_kubePods->clear();
    for (const KubePod &pod : snapshot.pods) {
        auto *item = new QTreeWidgetItem();
        item->setText(0, pod.name);
        item->setText(1, pod.ns);
        item->setText(2, pod.phase);
        item->setText(3, QString::number(pod.restarts));
        item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        if (kubePodIsIssue(pod)) {
            item->setForeground(0, QColor(QStringLiteral("#d1685f")));
        }
        m_kubePods->addTopLevelItem(item);
    }

    m_kubeDeploys->clear();
    for (const KubeDeployment &deployment : snapshot.deployments) {
        auto *item = new QTreeWidgetItem();
        item->setText(0, deployment.name);
        item->setText(1, deployment.ns);
        item->setText(2, QStringLiteral("%1/%2")
                            .arg(deployment.available)
                            .arg(deployment.desired));
        item->setTextAlignment(2, Qt::AlignRight | Qt::AlignVCenter);
        if (kubeDeploymentIsIssue(deployment)) {
            item->setForeground(0, QColor(QStringLiteral("#d1685f")));
        }
        m_kubeDeploys->addTopLevelItem(item);
    }

    const bool haveData =
        snapshot.state == KubeState::Ok || snapshot.stale;
    const QString events =
        haveData ? kubeEventsText(snapshot) : kubeSummaryText(snapshot);
    m_kubeEvents->setText(events.isEmpty() ? QStringLiteral("--") : events);

    m_kubeDiagnosis->setText(haveData ? kubeDiagnosisText(snapshot.diagnoses)
                                      : QStringLiteral("--"));
    m_kubeTimeline->setText(haveData ? kubeTimelineText(snapshot.timeline)
                                     : QStringLiteral("--"));
    m_deprecations->setText(
        haveData ? deprecationSummaryText(snapshot.deprecations)
                       + QStringLiteral("\n")
                       + deprecationDetailText(snapshot.deprecations)
                 : QStringLiteral("--"));
    m_nodes->setText(haveData ? kubeNodesText(snapshot.nodes)
                              : QStringLiteral("--"));

    if (haveData) {
        m_costSummary->setText(
            QStringLiteral("$%1/month  (%2 pods)")
                .arg(snapshot.costMonthly, 0, 'f', 2)
                .arg(snapshot.costPods));
        m_costWarnings->setText(
            snapshot.costMissingRequests > 0
                ? QStringLiteral("warn: %1 container(s) without cpu/memory request")
                      .arg(snapshot.costMissingRequests)
                : QStringLiteral("all containers declare requests"));
        m_costNamespaces->clear();
        for (const QPair<QString, double> &entry : snapshot.costByNamespace) {
            auto *item = new QTreeWidgetItem();
            item->setText(0, entry.first);
            item->setText(1, QStringLiteral("$%1").arg(entry.second, 0, 'f', 2));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            m_costNamespaces->addTopLevelItem(item);
        }
        m_costWorkloads->clear();
        for (const QPair<QString, double> &entry : snapshot.costTopWorkloads) {
            auto *item = new QTreeWidgetItem();
            item->setText(0, entry.first);
            item->setText(1, QStringLiteral("$%1").arg(entry.second, 0, 'f', 2));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            m_costWorkloads->addTopLevelItem(item);
        }
    } else {
        m_costSummary->setText(QStringLiteral("--"));
        m_costWarnings->setText(QStringLiteral("--"));
        m_costNamespaces->clear();
        m_costWorkloads->clear();
    }
}

void DashboardPanel::setServicesSnapshot(const ServicesSnapshot &snapshot) {
    m_servicesSummary->setText(servicesSummaryText(snapshot));

    QString text;
    int count = 0;
    for (const UnitRow &row : snapshot.units) {
        const bool failed = row.active == QStringLiteral("failed")
                            || row.sub == QStringLiteral("failed");
        const bool running = row.active == QStringLiteral("active")
                             && row.sub == QStringLiteral("running");
        if (!failed && !running) {
            continue;
        }
        text += QStringLiteral("%1%2  %3/%4\n")
                    .arg(failed ? QStringLiteral("! ") : QStringLiteral("  "),
                         row.name, row.active, row.sub);
        if (++count >= 40) {
            text += QStringLiteral("...\n");
            break;
        }
    }
    if (text.isEmpty()) {
        text = snapshot.available ? QStringLiteral("no services")
                                  : QStringLiteral("--");
    }
    m_services->setText(text);

    m_servicesChanges->setText(snapshot.available
                                   ? servicesChangesText(snapshot.changes)
                                   : QStringLiteral("--"));
}

void DashboardPanel::setJournalSnapshot(const JournalSnapshot &snapshot) {
    m_logsSummary->setText(journalSummaryText(snapshot));
    m_logs->setText(snapshot.available
                        ? journalLogsText(snapshot, 40)
                        : (m_osJournal ? QStringLiteral("--")
                                       : unsupportedOsText()));
    m_errors->setText(snapshot.available
                          ? journalErrorGroupsText(snapshot.errorGroups)
                          : (m_osJournal ? QStringLiteral("--")
                                         : unsupportedOsText()));

    QStringList messages;
    for (const JournalEntry &entry : snapshot.entries) {
        messages.append(entry.message);
    }
    const LogFacets facets = computeLogFacets(messages);
    m_logPipeline->setText(logPipelineSummaryText(facets)
                           + QStringLiteral("\n")
                           + logPipelineDetailText(facets));

    const SensitiveReport sensitive = scanSensitiveLines(messages);
    m_sensitiveSummary->setText(sensitiveSummaryText(sensitive));
    m_sensitive->setText(sensitiveDetailText(sensitive));

    QList<ThreatEvent> events;
    for (const JournalEntry &entry : snapshot.entries) {
        const ThreatEvent event =
            parseAuthEvent(entry.message, entry.timestampUsec / 1000);
        if (!event.logType.isEmpty()) {
            events.append(event);
        }
    }
    const ThreatReport threats = evaluateThreats(events);
    m_threats->setText(threatSummaryText(threats) + QStringLiteral("\n")
                       + threatDetailText(threats));
}

void DashboardPanel::setAlerts(const QList<Alert> &alerts) {
    if (alerts.isEmpty()) {
        m_alerts->setText(QStringLiteral("no active alerts"));
        return;
    }
    m_alerts->setTextFormat(Qt::RichText);
    QString text;
    for (const Alert &alert : alerts) {
        const QString severity = alert.severity.toUpper();
        const QRegularExpression severityWord(
            QStringLiteral("^(HIGH|CRIT|WARN|MEDIUM|LOW)$"));
        const bool known = severityWord.match(severity).hasMatch();
        const QString marker = known
            ? QStringLiteral("<span style=\"color:%1;\">[%2]</span>")
                .arg(ui_theme::statusColor(severity), severity)
            : QStringLiteral("[%1]").arg(severity);
        text += QStringLiteral("%1 %2&nbsp;&nbsp;%3<br/>")
                    .arg(marker, alert.title, alert.detail);
    }
    m_alerts->setText(text);
}

void DashboardPanel::setIncidents(const QList<Incident> &incidents) {
    m_incidentsSummary->setText(incidentsSummaryText(incidents));
    m_incidents->setText(incidentsDetailText(incidents));
}

void DashboardPanel::setSyntheticSnapshot(const SyntheticSnapshot &snapshot) {
    m_syntheticSummary->setText(syntheticSummaryText(snapshot));
    m_synthetic->setText(syntheticDetailText(snapshot));
}

void DashboardPanel::setTraceSnapshot(const TraceSnapshot &snapshot) {
    m_traceSummary->setText(traceSummaryText(snapshot));
    m_traceMap->setText(traceServiceMapText(snapshot));
    m_traceServices->setText(traceServicesText(snapshot));
    m_traceRecent->setText(traceRecentText(snapshot));
}

void DashboardPanel::setPathSnapshot(const PathSnapshot &snapshot) {
    m_pathSummary->setText(pathSummaryText(snapshot));
    m_pathTargets->setText(pathTargetsText(snapshot));
    m_pathHops->clear();
    if (snapshot.reports.isEmpty()) {
        return;
    }
    for (const PathHop &hop : snapshot.reports.first().hops) {
        auto *item = new QTreeWidgetItem();
        item->setText(0, QString::number(hop.index));
        item->setText(1, hop.host);
        item->setText(2, QString::number(hop.lossPercent, 'f', 1));
        item->setText(3, QString::number(hop.sent));
        item->setText(4, QString::number(hop.lastMs, 'f', 1));
        item->setText(5, QString::number(hop.avgMs, 'f', 1));
        item->setText(6, QString::number(hop.bestMs, 'f', 1));
        item->setText(7, QString::number(hop.worstMs, 'f', 1));
        item->setText(8, QString::number(hop.stdevMs, 'f', 1));
        for (int col = 0; col < 9; ++col) {
            item->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
        }
        if (hop.lossPercent > 0.0) {
            item->setForeground(1, QColor(QStringLiteral("#d1685f")));
        }
        m_pathHops->addTopLevelItem(item);
    }
}

void DashboardPanel::setStatsdSnapshot(const StatsdSnapshot &snapshot) {
    m_statsdSummary->setText(statsdSummaryText(snapshot));
    m_statsd->setText(snapshot.available ? statsdDetailText(snapshot)
                                         : QStringLiteral("--"));
}

void DashboardPanel::setDbmSnapshot(const DbmSnapshot &snapshot) {
    m_dbmSummary->setText(dbmSummaryText(snapshot));
    m_dbm->setText(dbmDetailText(snapshot));
    m_dbmStatements->setText(dbmStatementsText(snapshot));
}

void DashboardPanel::setTlsSnapshot(const TlsSnapshot &snapshot) {
    m_tlsSummary->setText(tlsSummaryText(snapshot));
    m_tls->setText(tlsDetailText(snapshot));
}

void DashboardPanel::setFimSnapshot(const FimSnapshot &snapshot) {
    m_fimSummary->setText(m_osFim ? fimSummaryText(snapshot)
                                  : unsupportedOsText());
    m_fim->setText(m_osFim ? fimDetailText(snapshot) : unsupportedOsText());
}
