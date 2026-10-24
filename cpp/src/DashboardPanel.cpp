#include "DashboardPanel.h"

#include "Theme.h"
#include "platform/PlatformFactory.h"

#include "LogParser.h"
#include "MemoryFormat.h"
#include "SensitiveStatus.h"
#include "Sparkline.h"
#include "TimeseriesChart.h"

#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

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

// Datadog KPI tile (img3/img4): small caption, huge bold value, delta
// line under the value. Value keeps its existing objectName so external
// assertions keep working.
QFrame *makeTile(const QString &caption, QLabel *value,
                 QLabel **deltaOut, QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("ddTile"));
    value->setObjectName(value->objectName().isEmpty()
                             ? QStringLiteral("ddTileValue")
                             : value->objectName());
    value->setFont(monoFont(30, true));
    value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(4);
    auto *captionLabel = makeTitle(caption, card);
    captionLabel->setFixedHeight(13);
    layout->addWidget(captionLabel);
    layout->addWidget(value, 1);
    if (deltaOut != nullptr) {
        auto *delta = new QLabel(QStringLiteral("--"), card);
        delta->setObjectName(QStringLiteral("ddDelta"));
        delta->setFont(monoFont(9));
        layout->addWidget(delta);
        *deltaOut = delta;
    }
    return card;
}

// Chart panel (img1/img2): bold white title row + expanding plot area.
QFrame *makeChartCard(const QString &title, QWidget *plot, QWidget *parent) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("ddCard"));
    card->setMinimumHeight(168);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 10, 6);
    layout->setSpacing(2);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("ddChartTitle"));
    titleLabel->setFont(monoFont(12, true));
    layout->addWidget(titleLabel);
    layout->addWidget(plot, 1);
    return card;
}

// Section banner (img4): centered white label on a full-width color
// strip with a chevron at the left edge.
// Tile delta line (Datadog style "▲ 18.6% vs window"): colored by
// direction; upIsBad selects red-on-up (throughput/utilization style)
// vs amber-on-up.
void setDeltaLabel(QLabel *label, const MetricStore &store,
                   double cadenceMs, bool upIsBad) {
    if (label == nullptr) {
        return;
    }
    if (store.size() < 2 || qFuzzyIsNull(store.values().constFirst())) {
        label->setText(QStringLiteral("--"));
        return;
    }
    const double first = store.values().constFirst();
    const double last = store.values().constLast();
    const double pct = (last - first) / qAbs(first) * 100.0;
    const int minutes = std::max(1, static_cast<int>(std::lround(
        (store.size() - 1) * cadenceMs / 60000.0)));
    const QChar arrow = pct >= 0 ? u'\u2191' : u'\u2193';
    const QColor color = pct >= 0
        ? (upIsBad ? QColor(ui_theme::crit()) : QColor(ui_theme::warn()))
        : QColor(ui_theme::ok());
    label->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    label->setText(QStringLiteral("%1 %2%  vs %3 min")
                       .arg(arrow)
                       .arg(qAbs(pct), 0, 'f', 1)
                       .arg(minutes));
}

QWidget *makeBanner(const QString &title, const QString &color,
                    QWidget *parent) {
    auto *strip = new QFrame(parent);
    strip->setObjectName(QStringLiteral("ddBanner"));
    strip->setStyleSheet(
        QStringLiteral("#ddBanner { background-color: %1; border-radius: 0;"
                       " }")
            .arg(color));
    auto *layout = new QHBoxLayout(strip);
    layout->setContentsMargins(10, 0, 10, 0);
    auto *label = new QLabel(title, strip);
    label->setFont(monoFont(11, true));
    label->setStyleSheet(QStringLiteral("color: #ffffff;"));
    label->setAlignment(Qt::AlignCenter);
    layout->addStretch(1);
    layout->addWidget(label);
    layout->addStretch(1);
    strip->setFixedHeight(26);
    return strip;
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

    m_timeChip = new QLabel(QStringLiteral("Past 2 s"), topbar);
    m_timeChip->setObjectName(QStringLiteral("ddTimechip"));
    m_timeChip->setFont(monoFont(9));

    m_pause = new QPushButton(QStringLiteral("PAUSE"), topbar);
    m_pause->setObjectName(QStringLiteral("ddPause"));
    m_pause->setCursor(Qt::PointingHandCursor);
    m_pause->setFocusPolicy(Qt::NoFocus);
    connect(m_pause, &QPushButton::clicked, this, [this] {
        m_chartsPaused = !m_chartsPaused;
        if (m_pause != nullptr) {
            m_pause->setText(m_chartsPaused ? QStringLiteral("RESUME")
                                            : QStringLiteral("PAUSE"));
        }
        for (TimeseriesChart *chart : {m_chartCpu, m_chartMem, m_chartProc,
                                       m_chartHostCpu, m_chartHostMem,
                                       m_chartLoad, m_chartNet, m_chartDisk,
                                       m_chartDiskUtil}) {
            if (chart != nullptr) {
                chart->setPaused(m_chartsPaused);
            }
        }
    });

    topLayout->addWidget(chip);
    topLayout->addSpacing(4);
    topLayout->addWidget(m_appValue);
    topLayout->addWidget(m_pidValue);
    topLayout->addStretch(1);
    topLayout->addWidget(m_timeChip);
    topLayout->addSpacing(4);
    topLayout->addWidget(m_pause);
    topLayout->addWidget(collapse);
    topLayout->addWidget(m_statusValue);
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

    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("ddFooter"));
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 3, 16, 3);
    m_footer = new QLabel(
        QStringLiteral("syspeek · local collection only · no telemetry · "
                       "◦ All Systems Operational"),
        footer);
    m_footer->setObjectName(QStringLiteral("ddFooterText"));
    m_footer->setFont(monoFont(9));
    m_footer->setAlignment(Qt::AlignCenter);
    footerLayout->addWidget(m_footer, 1);
    root->addWidget(footer);

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
        "#ddPause { color: %DIM%; background-color: transparent;"
        "  border: 1px solid %BORDER%; border-radius: 2px; padding: 4px 12px;"
        "  font-size: 9px; }"
        "#ddPause:hover { color: %TEXT%; border-color: %MUTED%; }"
        "#ddTimechip { color: %DIM%; background-color: transparent;"
        "  border: 1px solid %BORDER%; border-radius: 2px; padding: 4px 10px; }"
        "#ddTile { background-color: %SURFACE%; border-radius: 3px; }"
        "#ddDelta { color: %DIM%; }"
        "#ddChartTitle { color: %TEXT%; font-size: 12px; font-weight: 600; }"
        "#ddBanner { border-radius: 0; }"
        "#ddFooter { background-color: %BG%;"
        "  border-top: 1px solid %BORDER%; }"
        "#ddRail { background-color: %RAIL%; border: none;"
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
        "#ddStatMetrics, #ddStatEvents, #ddStatSvc, #ddAlOpen,"
        " #ddAlResolved, #ddIncOpen, #ddIncResolved { color: %TEXT%; }"
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
        "#ddIncidents { color: %MUTED%; }"
        "#ddTlsSummary { color: %DIM%; }"
        "#ddTls { color: %MUTED%; }"
        "#ddSensitiveSummary { color: %DIM%; }"
        "#ddSensitive { color: %MUTED%; }"
        "#ddSloSli, #ddSloObj, #ddSloBudget, #ddProcProcs, #ddProcThreads,"
        " #ddProcFds, #ddProcLoad { color: %TEXT%; }"
        "#ddDockRun, #ddDockTotal, #ddDockCgMem, #ddKubeTilePods,"
        " #ddKubeTileDeploys, #ddKubeTileCost { color: %TEXT%; }"
        "#ddMemTileTotal, #ddMemTileUsed, #ddMemTileAvail, #ddMemTileRss"
        " { color: %TEXT%; }"
        "#ddDiskTileRead, #ddDiskTileWrite, #ddDiskTileUtil, #ddDiskTilePsi,"
        " #ddHostTileCpu, #ddHostTileLoad, #ddHostTileAvail, #ddHostTileDisk,"
        " #ddNetTileConns, #ddNetTileEstab, #ddNetTileTx,"
        " #ddPathTileTargets, #ddPathTileHops, #ddPathTileLoss,"
        " #ddPathTileRtt, #ddSvcTileRunning, #ddSvcTileFailed,"
        " #ddSvcTileUnits, #ddSvcTileChanges { color: %TEXT%; }"
        "#ddLogTileEntries, #ddLogTileErrors, #ddLogTileLevels,"
        " #ddLogTilePipeline, #ddErrTileGroups, #ddErrTileCount,"
        " #ddErrTileWorst, #ddErrTileRate, #ddDepTileLibs, #ddDepTilePkg,"
        " #ddDepTileUnmapped, #ddDepTileRatio, #ddSweTilePkgs,"
        " #ddSweTileLibs, #ddSweTileResolved, #ddSweTileFams,"
        " #ddEpTileTotal, #ddEpTilePublic, #ddEpTileLocal, #ddEpTileDb,"
        " #ddSynTileChecks, #ddSynTileUp, #ddSynTileDown, #ddSynTileAvg,"
        " #ddInsTileAgents, #ddInsTileDd, #ddInsTileOtel, #ddInsTileOther,"
        " #ddTraceTileTraces, #ddTraceTileSpans, #ddTraceTileErrors,"
        " #ddTraceTileServices, #ddProfTileThreads, #ddProfTileCpu,"
        " #ddProfTileOffCpu, #ddProfTilePss, #ddDbTileCache,"
        " #ddDbTileCommits, #ddDbTileRollbacks, #ddDbTileDeadlocks,"
        " #ddSecTileExposure, #ddSecTileFindings, #ddSecTileThreats,"
        " #ddSecTileFim { color: %TEXT%; }"
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
        .replace(QStringLiteral("%RAIL%"), ui_theme::rail())
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
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- KPI tile row (img3/img4 style): caption, huge value, delta.
    m_cpuValue = new QLabel(QStringLiteral("--"), view);
    m_cpuValue->setObjectName(QStringLiteral("ddCpu"));
    m_memValue = new QLabel(QStringLiteral("--"), view);
    m_memValue->setObjectName(QStringLiteral("ddMem"));
    m_procCount = new QLabel(QStringLiteral("0"), view);
    m_procCount->setObjectName(QStringLiteral("ddProcCount"));
    m_statusBig = new QLabel(QStringLiteral("IDLE"), view);
    m_statusBig->setObjectName(QStringLiteral("ddStatusBig"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    auto *cpuTile = makeTile(QStringLiteral("CPU"), m_cpuValue,
                             &m_deltaCpu, view);
    auto *memTile = makeTile(QStringLiteral("MEMORY (RSS)"), m_memValue,
                             &m_deltaMem, view);
    auto *procTile = makeTile(QStringLiteral("PROCESSES"), m_procCount,
                              &m_deltaProc, view);
    auto *statusTile = makeTile(QStringLiteral("STATUS"), m_statusBig,
                                nullptr, view);
    tiles->addWidget(cpuTile, 1);
    tiles->addWidget(memTile, 1);
    tiles->addWidget(procTile, 1);
    tiles->addWidget(statusTile, 1);
    layout->addLayout(tiles);

    // --- Chart panels in the DD three-across grid (img1/img2).
    m_chartCpu = new TimeseriesChart(view);
    m_chartCpu->setUnit(QStringLiteral("Percent"));
    m_chartCpu->setSeriesName(QStringLiteral("syspeek.cpu.percent"));
    m_chartMem = new TimeseriesChart(view);
    m_chartMem->setUnit(QStringLiteral("Megabytes"));
    m_chartMem->setSeriesName(QStringLiteral("syspeek.mem.rss"));
    m_chartProc = new TimeseriesChart(view);
    m_chartProc->setUnit(QStringLiteral("Processes"));
    m_chartProc->setSeriesName(QStringLiteral("syspeek.process.count"));
    m_chartProc->setBarMode(true);

    layout->addWidget(makeBanner(QStringLiteral("FOCUSED APP"),
        ui_theme::accent(), view));
    auto *focusRow = new QHBoxLayout();
    focusRow->setSpacing(10);
    focusRow->addWidget(
        makeChartCard(QStringLiteral("CPU (%)"), m_chartCpu, view), 1);
    focusRow->addWidget(
        makeChartCard(QStringLiteral("Memory (RSS, MB)"), m_chartMem, view), 1);
    focusRow->addWidget(
        makeChartCard(QStringLiteral("Process count"), m_chartProc, view), 1);
    layout->addLayout(focusRow);

    m_chartHostCpu = new TimeseriesChart(view);
    m_chartHostCpu->setUnit(QStringLiteral("Percent"));
    m_chartHostCpu->setSeriesName(QStringLiteral("system.cpu.idle(*)"));
    m_chartHostMem = new TimeseriesChart(view);
    m_chartHostMem->setUnit(QStringLiteral("Gigabytes"));
    m_chartHostMem->setSeriesName(QStringLiteral("system.mem.available"));
    m_chartLoad = new TimeseriesChart(view);
    m_chartLoad->setUnit(QStringLiteral("Load"));
    m_chartLoad->setSeriesName(QStringLiteral("system.load.1"));
    m_chartLoad->setBarMode(true);

    layout->addWidget(makeBanner(QStringLiteral("HOST"),
        ui_theme::banner(), view));
    auto *hostRow = new QHBoxLayout();
    hostRow->setSpacing(10);
    hostRow->addWidget(
        makeChartCard(QStringLiteral("Host CPU (%)"), m_chartHostCpu, view),
        1);
    hostRow->addWidget(
        makeChartCard(QStringLiteral("Memory available (GB)"),
                      m_chartHostMem, view),
        1);
    hostRow->addWidget(
        makeChartCard(QStringLiteral("Load (1m)"), m_chartLoad, view), 1);
    layout->addLayout(hostRow);

    m_chartNet = new TimeseriesChart(view);
    m_chartNet->setUnit(QStringLiteral("Kilobytes/second"));
    m_chartNet->setSeriesName(QStringLiteral("system.net.rx"));
    m_chartNet->setSeriesNameB(QStringLiteral("system.net.tx"));
    m_chartDisk = new TimeseriesChart(view);
    m_chartDisk->setUnit(QStringLiteral("Kilobytes/second"));
    m_chartDisk->setSeriesName(QStringLiteral("system.disk.read"));
    m_chartDisk->setSeriesNameB(QStringLiteral("system.disk.write"));
    m_chartDiskUtil = new TimeseriesChart(view);
    m_chartDiskUtil->setUnit(QStringLiteral("Percent"));
    m_chartDiskUtil->setSeriesName(QStringLiteral("system.disk.util"));

    layout->addWidget(makeBanner(QStringLiteral("I/O & NETWORK"),
        ui_theme::warn(), view));
    auto *ioRow = new QHBoxLayout();
    ioRow->setSpacing(10);
    ioRow->addWidget(
        makeChartCard(QStringLiteral("Network (kB/s)"), m_chartNet, view), 1);
    ioRow->addWidget(
        makeChartCard(QStringLiteral("Disk read/write (kB/s)"), m_chartDisk,
                      view),
        1);
    ioRow->addWidget(
        makeChartCard(QStringLiteral("Disk utilization (%)"),
                      m_chartDiskUtil, view),
        1);
    layout->addLayout(ioRow);

    // --- Text insight cards carried over (Datadog text panels).
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

    // Compact KPI line row (values the tests pin by objectName).
    auto *kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(10);
    kpiRow->addWidget(makeStatCard(QStringLiteral("HOST CPU"),
                                   hostCpuContent, view), 1);
    kpiRow->addWidget(makeStatCard(QStringLiteral("MEM AVAILABLE"),
                                   m_hostMem, view), 1);
    kpiRow->addWidget(makeStatCard(QStringLiteral("DISK FREE"), m_hostDisk,
                                   view),
                      1);
    kpiRow->addWidget(makeStatCard(QStringLiteral("NET rx / tx"), m_hostNet,
                                   view),
                      1);
    kpiRow->addWidget(makeStatCard(QStringLiteral("LOAD 1m"), m_hostLoad,
                                   view),
                      1);
    layout->addLayout(kpiRow);

    // Watchdog analytic section: two-across grid + insights card.
    m_watchdog = new QLabel(QStringLiteral("--"), view);
    m_watchdog->setObjectName(QStringLiteral("ddWatchdog"));
    m_watchdog->setFont(monoFont(10));
    m_watchdog->setWordWrap(false);
    m_runtime = new QLabel(QStringLiteral("--"), view);
    m_runtime->setObjectName(QStringLiteral("ddRuntime"));
    m_runtime->setFont(monoFont(10));
    m_runtime->setWordWrap(false);
    m_forecast = new QLabel(QStringLiteral("--"), view);
    m_forecast->setObjectName(QStringLiteral("ddForecast"));
    m_forecast->setFont(monoFont(10));
    m_forecast->setWordWrap(false);
    m_insights = new QLabel(QStringLiteral("--"), view);
    m_insights->setObjectName(QStringLiteral("ddInsights"));
    m_insights->setFont(monoFont(10));
    m_insights->setWordWrap(false);

    auto *insightGrid = new QGridLayout();
    insightGrid->setSpacing(10);
    insightGrid->addWidget(
        makeCard(QStringLiteral("WATCHDOG (anomaly detection)"), m_watchdog,
                 view),
        0, 0);
    insightGrid->addWidget(
        makeCard(QStringLiteral("RUNTIME (SSI)"), m_runtime, view), 0, 1);
    insightGrid->addWidget(
        makeCard(QStringLiteral("FORECAST (predictive)"), m_forecast, view),
        1, 0);
    insightGrid->addWidget(
        makeCard(QStringLiteral("WATCHDOG INSIGHTS"), m_insights, view), 1, 1);
    layout->addLayout(insightGrid, 2);

    // Fit the viewport width: width-minimum clamps let long mono lines
    // clip instead of widening the page (heights stay natural).
    for (QLabel *label : {m_hostCpu, m_hostMem, m_hostDisk, m_hostNet,
                          m_hostLoad, m_watchdog, m_runtime, m_forecast,
                          m_insights}) {
        label->setMinimumWidth(1);
    }

    layout->addStretch(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral("#ddScrollContent { background-color: %1; }")
        .arg(ui_theme::bg()));
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

void DashboardPanel::updateOverviewText(const DisplaySnapshot &snapshot) {
    setDeltaLabel(m_deltaCpu, m_cpuStore, m_cadenceMs, true);
    setDeltaLabel(m_deltaMem, m_memStore, m_cadenceMs, false);
    setDeltaLabel(m_deltaProc, m_procStore, m_cadenceMs, false);

    if (m_cpuValue != nullptr && snapshot.cpuValid) {
        const QString c =
            snapshot.cpuPercent >= 90.0
                ? ui_theme::crit()
                : (snapshot.cpuPercent >= 78.0 ? ui_theme::warn()
                                               : ui_theme::text());
        m_cpuValue->setStyleSheet(
            QStringLiteral("color: %1;").arg(c));
    }
    if (m_statusBig != nullptr) {
        m_statusBig->setStyleSheet(
            QStringLiteral("color: %1;").arg(statusColor(snapshot.status)));
    }

    if (m_timeChip != nullptr && m_cadenceMs > 0.0) {
        const int samples = static_cast<int>(m_procStore.size());
        const int minutes = std::max(1, static_cast<int>(std::lround(
            samples * m_cadenceMs / 60000.0)));
        m_timeChip->setText(minutes >= 55
            ? QStringLiteral("Past 1 Hour")
            : QStringLiteral("Past %1 Min").arg(minutes));
    }

    if (m_footer != nullptr) {
        const QString green = QStringLiteral("<span style=\"color:%1\">●</span>")
            .arg(ui_theme::ok());
        const QString amber = QStringLiteral("<span style=\"color:%1\">●</span>")
            .arg(ui_theme::warn());
        const QString tail = snapshot.hostAvailable
            ? QStringLiteral("%1 All Systems Operational").arg(green)
            : (snapshot.hostWarming
                ? QStringLiteral("%1 Collector warming up").arg(amber)
                : QStringLiteral("%1 Host data unavailable").arg(amber));
        m_footer->setText(QStringLiteral(
                              "syspeek %1 · local collection only · "
                              "no telemetry · %2")
                              .arg(QCoreApplication::applicationVersion())
                              .arg(tail));
    }
}

QWidget *DashboardPanel::buildMetricsView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- KPI tile row (img3 style, receiver telemetry vocabulary).
    m_statMetrics = new QLabel(QStringLiteral("0"), view);
    m_statMetrics->setObjectName(QStringLiteral("ddStatMetrics"));
    m_statEvents = new QLabel(QStringLiteral("0"), view);
    m_statEvents->setObjectName(QStringLiteral("ddStatEvents"));
    m_statSvc = new QLabel(QStringLiteral("0"), view);
    m_statSvc->setObjectName(QStringLiteral("ddStatSvc"));
    m_statErrors = new QLabel(QStringLiteral("0"), view);
    m_statErrors->setObjectName(QStringLiteral("ddStatErrors"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("METRICS RECEIVED"),
                              m_statMetrics, &m_deltaMetrics, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("EVENTS"), m_statEvents,
                              &m_deltaEvents, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("SERVICE CHECKS"), m_statSvc,
                              &m_deltaSvc, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("PARSE ERRORS"), m_statErrors,
                              &m_deltaErr, view), 1);
    layout->addLayout(tiles);

    // --- RECEIVER line (kept label; DD status-section vocabulary).
    m_statsdSummary = new QLabel(QStringLiteral("--"), view);
    m_statsdSummary->setObjectName(QStringLiteral("ddStatsdSummary"));
    m_statsdSummary->setFont(monoFont(13, true));
    layout->addWidget(makeBanner(QStringLiteral("DOGSTATSD RECEIVER"),
        ui_theme::accent(), view));
    layout->addWidget(makeCard(
        QStringLiteral("receiver (127.0.0.1:8125)"), m_statsdSummary, view));

    // --- Telemetry chart rows (per-tick ingest rates, img2 panels).
    m_chartStatRate = new TimeseriesChart(view);
    m_chartStatRate->setUnit(QStringLiteral("Metrics/second"));
    m_chartStatRate->setSeriesName(QStringLiteral("dogstatsd.metrics"));
    m_chartStatRate->setBarMode(true);
    m_chartStatEvents = new TimeseriesChart(view);
    m_chartStatEvents->setUnit(QStringLiteral("Events/second"));
    m_chartStatEvents->setSeriesName(
        QStringLiteral("dogstatsd.events_received"));
    m_chartStatEvents->setSeriesNameB(
        QStringLiteral("dogstatsd.checks_received"));
    m_chartStatErrors = new TimeseriesChart(view);
    m_chartStatErrors->setUnit(QStringLiteral("Errors"));
    m_chartStatErrors->setSeriesName(
        QStringLiteral("dogstatsd.parse_errors"));
    m_chartStatErrors->setSeriesColors(QColor(ui_theme::crit()),
                                       QColor(ui_theme::seriesB()));

    layout->addWidget(makeBanner(QStringLiteral("TELEMETRY"),
        ui_theme::banner(), view));
    auto *teleRow = new QHBoxLayout();
    teleRow->setSpacing(10);
    teleRow->addWidget(makeChartCard(QStringLiteral("Metrics ingested"),
                                     m_chartStatRate, view), 1);
    teleRow->addWidget(makeChartCard(QStringLiteral("Events & service checks"),
                                     m_chartStatEvents, view), 1);
    teleRow->addWidget(makeChartCard(QStringLiteral("Parse errors"),
                                     m_chartStatErrors, view), 1);
    layout->addLayout(teleRow);

    // --- Series table (Metrics-Explorer style, img2 naming).
    m_statsdSeriesTree = new QTreeWidget(view);
    m_statsdSeriesTree->setObjectName(QStringLiteral("ddStatTree"));
    m_statsdSeriesTree->setColumnCount(5);
    m_statsdSeriesTree->setHeaderLabels({QStringLiteral("Metric"),
                                         QStringLiteral("Type"),
                                         QStringLiteral("Tags"),
                                         QStringLiteral("Value"),
                                         QStringLiteral("Count")});
    m_statsdSeriesTree->setRootIsDecorated(false);
    m_statsdSeriesTree->setSelectionMode(QAbstractItemView::NoSelection);
    m_statsdSeriesTree->setFocusPolicy(Qt::NoFocus);
    m_statsdSeriesTree->header()->setSectionResizeMode(
        0, QHeaderView::Stretch);
    m_statsdSeriesTree->header()->setSectionResizeMode(
        2, QHeaderView::Stretch);
    for (int i : {1, 3, 4}) {
        m_statsdSeriesTree->header()->setSectionResizeMode(
            i, QHeaderView::ResizeToContents);
    }

    m_statsd = new QLabel(QStringLiteral("--"), view);
    m_statsd->setObjectName(QStringLiteral("ddStatsd"));
    m_statsd->setFont(monoFont(10));
    m_statsd->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("SERIES"),
        ui_theme::warn(), view));
    layout->addWidget(makeCard(QStringLiteral("SERIES EXPLORER"),
                               m_statsdSeriesTree, view), 2);
    layout->addWidget(makeCard(QStringLiteral("DETAIL"), m_statsd, view), 1);

    // Fit the viewport width: width-only clamps (heights stay natural;
    // min 1x1 collapses labels to zero height, M61 triage).
    m_statsdSummary->setMinimumWidth(1);
    m_statsd->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildSloView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Attainment tile row (Nobl9 model: objective/SLI/budget/burn).
    m_sloSli = new QLabel(QStringLiteral("--"), view);
    m_sloSli->setObjectName(QStringLiteral("ddSloSli"));
    m_sloObj = new QLabel(QStringLiteral("--"), view);
    m_sloObj->setObjectName(QStringLiteral("ddSloObj"));
    m_sloBudget = new QLabel(QStringLiteral("--"), view);
    m_sloBudget->setObjectName(QStringLiteral("ddSloBudget"));
    m_sloBurn = new QLabel(QStringLiteral("--"), view);
    m_sloBurn->setObjectName(QStringLiteral("ddSloBurn"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("SLI"), m_sloSli,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("OBJECTIVE"), m_sloObj,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("BUDGET LEFT"),
                              m_sloBudget, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("BURN RATE"), m_sloBurn,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    // --- Attainment card (name + met/exhausted chip + counts).
    m_sloSummary = new QLabel(QStringLiteral("--"), view);
    m_sloSummary->setObjectName(QStringLiteral("ddSloSummary"));
    m_sloSummary->setFont(monoFont(11));
    m_sloSummary->setTextFormat(Qt::RichText);
    m_sloSummary->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("SLO ATTAINMENT"),
        ui_theme::accent(), view));
    layout->addWidget(makeCard(QStringLiteral("AVAILABILITY"),
                               m_sloSummary, view), 1);

    // --- SLI/budget history (Syspeek-own rolling counters).
    m_chartSlo = new TimeseriesChart(view);
    m_chartSlo->setUnit(QStringLiteral("Percent"));
    m_chartSlo->setSeriesName(QStringLiteral("syspeek.slo.sli"));
    m_chartSlo->setSeriesNameB(QStringLiteral("syspeek.slo.budget"));

    layout->addWidget(makeBanner(QStringLiteral("SLI / ERROR BUDGET"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("SLI & budget remaining"),
                                    m_chartSlo, view));

    // --- Per-window burn rates (colored by exhaustion speed).
    m_slo = new QLabel(QStringLiteral("--"), view);
    m_slo->setObjectName(QStringLiteral("ddSlo"));
    m_slo->setFont(monoFont(10));
    m_slo->setTextFormat(Qt::RichText);
    m_slo->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("BURN WINDOWS"),
        ui_theme::warn(), view));
    layout->addWidget(makeCard(QStringLiteral("BURN RATE BY WINDOW"),
                               m_slo, view), 2);

    // Fit the viewport width: width-only clamps (heights stay natural).
    m_sloSummary->setMinimumWidth(1);
    m_slo->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildAlertsView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Monitor-status tile row (status:critical / status:warning
    // vocabulary from the Datadog agent monitor telemetry).
    m_alOpen = new QLabel(QStringLiteral("0"), view);
    m_alOpen->setObjectName(QStringLiteral("ddAlOpen"));
    m_alCrit = new QLabel(QStringLiteral("0"), view);
    m_alCrit->setObjectName(QStringLiteral("ddAlCrit"));
    m_alWarn = new QLabel(QStringLiteral("0"), view);
    m_alWarn->setObjectName(QStringLiteral("ddAlWarn"));
    m_alResolved = new QLabel(QStringLiteral("0"), view);
    m_alResolved->setObjectName(QStringLiteral("ddAlResolved"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("OPEN ALERTS"),
                              m_alOpen, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("CRITICAL"),
                              m_alCrit, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("WARNINGS"),
                              m_alWarn, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("RESOLVED"),
                              m_alResolved, nullptr, view), 1);
    layout->addLayout(tiles);

    // --- Active monitor list (severity chips stay rich-text colored;
    // the M60 #ddAlerts QSS rule removal keeps spans colorized).
    m_alerts = new QLabel(QStringLiteral("no active alerts"), view);
    m_alerts->setObjectName(QStringLiteral("ddAlerts"));
    m_alerts->setFont(monoFont(11));
    m_alerts->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("ACTIVE ALERTS"),
        ui_theme::accent(), view));
    layout->addWidget(makeCard(QStringLiteral("MONITOR STATUS"),
                               m_alerts, view), 2);

    // --- Transition telemetry (ours; counter names in the agent family
    // monitor_id / datadogmonitors / notifications_sent).
    m_chartAlertTrans = new TimeseriesChart(view);
    m_chartAlertTrans->setUnit(QStringLiteral("Transitions"));
    m_chartAlertTrans->setSeriesName(QStringLiteral("syspeek.alerts.fired"));
    m_chartAlertTrans->setSeriesNameB(QStringLiteral("syspeek.alerts.resolved"));
    m_chartAlertTrans->setBarMode(true);

    layout->addWidget(makeBanner(QStringLiteral("TRANSITIONS"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("Alerts fired & resolved"),
                                    m_chartAlertTrans, view));

    // --- Panel-side event timeline (fired/resolved marks with timestamps).
    m_alertTimeline = new QLabel(QStringLiteral("--"), view);
    m_alertTimeline->setObjectName(QStringLiteral("ddAlertTimeline"));
    m_alertTimeline->setFont(monoFont(10));
    m_alertTimeline->setTextFormat(Qt::RichText);
    m_alertTimeline->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("TIMELINE"),
        ui_theme::warn(), view));
    layout->addWidget(makeCard(QStringLiteral("EVENT HISTORY"),
                               m_alertTimeline, view), 1);

    // Fit the viewport width: width-only clamps (heights stay natural).
    m_alerts->setMinimumWidth(1);
    m_alertTimeline->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildIncidentsView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Incident-status tile row (Alertmanager-style grouping, M35;
    // severity vocabulary per the agent strings).
    m_incOpen = new QLabel(QStringLiteral("0"), view);
    m_incOpen->setObjectName(QStringLiteral("ddIncOpen"));
    m_incCrit = new QLabel(QStringLiteral("0"), view);
    m_incCrit->setObjectName(QStringLiteral("ddIncCrit"));
    m_incWarn = new QLabel(QStringLiteral("0"), view);
    m_incWarn->setObjectName(QStringLiteral("ddIncWarn"));
    m_incResolved = new QLabel(QStringLiteral("0"), view);
    m_incResolved->setObjectName(QStringLiteral("ddIncResolved"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("OPEN INCIDENTS"),
                              m_incOpen, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("CRITICAL"),
                              m_incCrit, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("WARNINGS"),
                              m_incWarn, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("RESOLVED"),
                              m_incResolved, nullptr, view), 1);
    layout->addLayout(tiles);

    // --- Active incident list (grouped Alertmanager-style rows).
    m_incidentsSummary = new QLabel(QStringLiteral("--"), view);
    m_incidentsSummary->setObjectName(QStringLiteral("ddIncidentsSummary"));
    m_incidentsSummary->setFont(monoFont(11));
    m_incidentsSummary->setTextFormat(Qt::RichText);
    m_incidentsSummary->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("OPEN INCIDENTS"),
        ui_theme::accent(), view));
    layout->addWidget(makeCard(QStringLiteral("GROUPED BY ALERT"),
                               m_incidentsSummary, view), 2);

    // --- Open-incident count per tick (Syspeek-own history counter).
    m_chartIncOpen = new TimeseriesChart(view);
    m_chartIncOpen->setUnit(QStringLiteral("Incidents"));
    m_chartIncOpen->setSeriesName(QStringLiteral("syspeek.incidents.open"));

    layout->addWidget(makeBanner(QStringLiteral("OPEN OVER TIME"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("Open incidents"),
                                    m_chartIncOpen, view));

    m_incidents = new QLabel(QStringLiteral("no incidents"), view);
    m_incidents->setObjectName(QStringLiteral("ddIncidents"));
    m_incidents->setFont(monoFont(10));
    m_incidents->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("HISTORY"),
        ui_theme::warn(), view));
    layout->addWidget(makeCard(QStringLiteral("TRACKER SNAPSHOTS"),
                               m_incidents, view), 1);

    // Fit the viewport width: width-only clamps (heights stay natural).
    m_incidentsSummary->setMinimumWidth(1);
    m_incidents->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildProfilesView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Thread-profile tiles (M37: thread-level cpu/off-cpu/pss).
    m_profTileThreads = new QLabel(QStringLiteral("--"), view);
    m_profTileThreads->setObjectName(QStringLiteral("ddProfTileThreads"));
    m_profTileCpu = new QLabel(QStringLiteral("--"), view);
    m_profTileCpu->setObjectName(QStringLiteral("ddProfTileCpu"));
    m_profTileOffCpu = new QLabel(QStringLiteral("--"), view);
    m_profTileOffCpu->setObjectName(QStringLiteral("ddProfTileOffCpu"));
    m_profTilePss = new QLabel(QStringLiteral("--"), view);
    m_profTilePss->setObjectName(QStringLiteral("ddProfTilePss"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("THREADS"), m_profTileThreads,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("CPU %"), m_profTileCpu,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("OFF-CPU"), m_profTileOffCpu,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("PSS MB"), m_profTilePss,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_profileSummary = new QLabel(QStringLiteral("--"), view);
    m_profileSummary->setObjectName(QStringLiteral("ddProfileSummary"));
    m_profileSummary->setFont(monoFont(13, true));

    layout->addWidget(makeBanner(QStringLiteral("PROFILES"),
        ui_theme::accent(), view));
    layout->addWidget(makeCard(
        QStringLiteral("PROFILES (threads / off-CPU / memory)"),
        m_profileSummary, view));

    // --- Thread-cpu history (thread-level; the agent cannot sample
    // cross-process threads unprivileged - Syspeek-own series).
    m_chartProfile = new TimeseriesChart(view);
    m_chartProfile->setUnit(QStringLiteral("Percent"));
    m_chartProfile->setSeriesName(QStringLiteral("syspeek.profile.cpu"));

    layout->addWidget(makeBanner(QStringLiteral("CPU OVER TIME"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("total thread cpu"),
                                    m_chartProfile, view));

    layout->addWidget(makeBanner(QStringLiteral("THREAD DETAIL"),
        ui_theme::warn(), view));
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

    m_profileThreads->setMinimumWidth(1);
    m_profileOffCpu->setMinimumWidth(1);
    m_profileMemory->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildProcessesView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Fleet totals, one tile per resource axis.
    m_procTileProcs = new QLabel(QStringLiteral("--"), view);
    m_procTileProcs->setObjectName(QStringLiteral("ddProcProcs"));
    m_procTileThreads = new QLabel(QStringLiteral("--"), view);
    m_procTileThreads->setObjectName(QStringLiteral("ddProcThreads"));
    m_procTileFds = new QLabel(QStringLiteral("--"), view);
    m_procTileFds->setObjectName(QStringLiteral("ddProcFds"));
    m_procTileLoad = new QLabel(QStringLiteral("--"), view);
    m_procTileLoad->setObjectName(QStringLiteral("ddProcLoad"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("PROCESSES"), m_procTileProcs,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("THREADS"), m_procTileThreads,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("OPEN FDS"), m_procTileFds,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("LOAD 1M"), m_procTileLoad,
                              nullptr, view), 1);
    layout->addLayout(tiles);

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

    layout->addWidget(makeBanner(QStringLiteral("PROCESS TREE"),
        ui_theme::accent(), view));
    layout->addWidget(makeCard(QStringLiteral("PROCESSES"), m_procs, view), 2);

    // --- CPU / load history (Syspeek-own fleet counters).
    m_chartProcTotals = new TimeseriesChart(view);
    m_chartProcTotals->setUnit(QStringLiteral("Percent"));
    m_chartProcTotals->setSeriesName(QStringLiteral("syspeek.procs.cpu"));
    m_chartProcTotals->setSeriesNameB(QStringLiteral("syspeek.host.load1"));

    layout->addWidget(makeBanner(QStringLiteral("CPU / LOAD"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("fleet CPU vs load-1m"),
                                    m_chartProcTotals, view));

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

    layout->addWidget(makeBanner(QStringLiteral("FOCUS DETAIL"),
        ui_theme::warn(), view));
    layout->addLayout(detailRow);

    // Fit the viewport width: width-only clamps (heights stay natural).
    m_focusDetail->setMinimumWidth(1);
    m_openFiles->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildMemoryView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Host-fleet tiles + the focused app's RSS anchor; "used" is
    // honest arithmetic (total - available, cache not split yet).
    m_memTileTotal = new QLabel(QStringLiteral("--"), view);
    m_memTileTotal->setObjectName(QStringLiteral("ddMemTileTotal"));
    m_memTileUsed = new QLabel(QStringLiteral("--"), view);
    m_memTileUsed->setObjectName(QStringLiteral("ddMemTileUsed"));
    m_memTileAvail = new QLabel(QStringLiteral("--"), view);
    m_memTileAvail->setObjectName(QStringLiteral("ddMemTileAvail"));
    m_memTileRss = new QLabel(QStringLiteral("--"), view);
    m_memTileRss->setObjectName(QStringLiteral("ddMemTileRss"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("HOST TOTAL"), m_memTileTotal,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("HOST USED"), m_memTileUsed,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("HOST AVAIL"), m_memTileAvail,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("APP RSS"), m_memTileRss,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_memDetail = new QLabel(QStringLiteral("--"), view);
    m_memDetail->setObjectName(QStringLiteral("ddMemDetail"));
    m_memDetail->setFont(monoFont(12));

    layout->addWidget(makeBanner(QStringLiteral("APP MEMORY"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("MEMORY BREAKDOWN"), m_memDetail, view), 1);

    // --- Used vs available history (Syspeek-own counters).
    m_chartMemPage = new TimeseriesChart(view);
    m_chartMemPage->setUnit(QStringLiteral("GB"));
    m_chartMemPage->setSeriesName(QStringLiteral("syspeek.mem.used"));
    m_chartMemPage->setSeriesNameB(QStringLiteral("syspeek.mem.available"));

    layout->addWidget(makeBanner(QStringLiteral("HOST USED / AVAILABLE"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("host memory in use vs "
        "available"), m_chartMemPage, view));

    m_memDetail->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildNetworkView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Socket-fleet tiles; retrans carries the TCP-health gate.
    m_netTileConns = new QLabel(QStringLiteral("--"), view);
    m_netTileConns->setObjectName(QStringLiteral("ddNetTileConns"));
    m_netTileEstab = new QLabel(QStringLiteral("--"), view);
    m_netTileEstab->setObjectName(QStringLiteral("ddNetTileEstab"));
    m_netTileRetrans = new QLabel(QStringLiteral("--"), view);
    m_netTileRetrans->setObjectName(QStringLiteral("ddNetTileRetrans"));
    m_netTileTx = new QLabel(QStringLiteral("--"), view);
    m_netTileTx->setObjectName(QStringLiteral("ddNetTileTx"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("CONNS"), m_netTileConns,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("ESTABLISHED"), m_netTileEstab,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("RETRANS %"), m_netTileRetrans,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("TX KB/S"), m_netTileTx,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_net = new QLabel(QStringLiteral("--"), view);
    m_net->setObjectName(QStringLiteral("ddNet"));
    m_net->setFont(monoFont(11));

    layout->addWidget(makeBanner(QStringLiteral("CONNECTIONS (focused app)"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("CONNECTIONS"), m_net, view), 2);

    // --- Host throughput (agent-grounded series vocabulary).
    m_chartNetPage = new TimeseriesChart(view);
    m_chartNetPage->setUnit(QStringLiteral("kB/s"));
    m_chartNetPage->setSeriesName(QStringLiteral("system.net.bytes_rcvd"));
    m_chartNetPage->setSeriesNameB(QStringLiteral("system.net.bytes_sent"));

    layout->addWidget(makeBanner(QStringLiteral("THROUGHPUT"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("host rx + tx"),
                                    m_chartNetPage, view));

    // --- Focused-app connection latency trend (netlink tcp_info per tick).
    m_chartConnRtt = new TimeseriesChart(view);
    m_chartConnRtt->setUnit(QStringLiteral("ms"));
    m_chartConnRtt->setSeriesName(QStringLiteral("syspeek.conn.rtt"));
    layout->addWidget(makeBanner(QStringLiteral("LATENCY (focused)"),
        ui_theme::warn(), view));
    layout->addWidget(makeChartCard(QStringLiteral("avg tcp_info rtt"),
                                    m_chartConnRtt, view));

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

    auto *healthRow = new QHBoxLayout();
    healthRow->setSpacing(14);
    m_ifaces = new QLabel(QStringLiteral("--"), view);
    m_ifaces->setObjectName(QStringLiteral("ddIfaces"));
    m_ifaces->setFont(monoFont(10));
    healthRow->addWidget(
        makeCard(QStringLiteral("INTERFACES"), m_ifaces, view), 1);

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
    healthRow->addWidget(
        makeCard(QStringLiteral("TCP HEALTH"), tcpBox, view), 1);
    layout->addLayout(healthRow, 1);

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
    layout->addWidget(
        makeCard(QStringLiteral("TLS CERTIFICATES"), tlsBox, view), 1);

    m_net->setMinimumWidth(1);
    m_listen->setMinimumWidth(1);
    m_states->setMinimumWidth(1);
    m_ifaces->setMinimumWidth(1);
    m_tcp->setMinimumWidth(1);
    m_tls->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildNetworkPathView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Probe tiles; loss colored red when any hop drops.
    m_pathTileTargets = new QLabel(QStringLiteral("--"), view);
    m_pathTileTargets->setObjectName(QStringLiteral("ddPathTileTargets"));
    m_pathTileHops = new QLabel(QStringLiteral("--"), view);
    m_pathTileHops->setObjectName(QStringLiteral("ddPathTileHops"));
    m_pathTileLoss = new QLabel(QStringLiteral("--"), view);
    m_pathTileLoss->setObjectName(QStringLiteral("ddPathTileLoss"));
    m_pathTileRtt = new QLabel(QStringLiteral("--"), view);
    m_pathTileRtt->setObjectName(QStringLiteral("ddPathTileRtt"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("TARGETS"), m_pathTileTargets,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("HOPS"), m_pathTileHops,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("LOSS MAX"), m_pathTileLoss,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("RTT AVG"), m_pathTileRtt,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_pathSummary = new QLabel(QStringLiteral("--"), view);
    m_pathSummary->setObjectName(QStringLiteral("ddPathSummary"));
    m_pathSummary->setFont(monoFont(13, true));

    m_pathTargets = new QLabel(QStringLiteral("--"), view);
    m_pathTargets->setObjectName(QStringLiteral("ddPathTargets"));
    m_pathTargets->setFont(monoFont(10));
    m_pathTargets->setWordWrap(false);

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

    layout->addWidget(makeBanner(QStringLiteral("NETWORK PATH"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("NETWORK PATH (mtr)"), m_pathSummary, view), 1);
    layout->addWidget(
        makeCard(QStringLiteral("TARGETS"), m_pathTargets, view), 1);

    // --- End-to-end latency history (mtr final-hop avg; Syspeek-own
    // counter - the runner is local mtr, the agent ships no equivalent).
    m_chartPath = new TimeseriesChart(view);
    m_chartPath->setUnit(QStringLiteral("ms"));
    m_chartPath->setSeriesName(QStringLiteral("syspeek.path.rtt"));

    layout->addWidget(makeBanner(QStringLiteral("LATENCY OVER TIME"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("end-to-end avg RTT"),
                                    m_chartPath, view));

    layout->addWidget(makeBanner(QStringLiteral("HOP TABLE"),
        ui_theme::warn(), view));
    layout->addWidget(makeCard(QStringLiteral("HOPS"), m_pathHops, view), 2);

    m_pathSummary->setMinimumWidth(1);
    m_pathTargets->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildContainersView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Container-fleet tiles; CGROUP MEM is the focused app's cgroup.
    m_dockTileRun = new QLabel(QStringLiteral("--"), view);
    m_dockTileRun->setObjectName(QStringLiteral("ddDockRun"));
    m_dockTileIssues = new QLabel(QStringLiteral("--"), view);
    m_dockTileIssues->setObjectName(QStringLiteral("ddDockIssues"));
    m_dockTileTotal = new QLabel(QStringLiteral("--"), view);
    m_dockTileTotal->setObjectName(QStringLiteral("ddDockTotal"));
    m_dockTileCgMem = new QLabel(QStringLiteral("--"), view);
    m_dockTileCgMem->setObjectName(QStringLiteral("ddDockCgMem"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("RUNNING"), m_dockTileRun,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("ISSUES"), m_dockTileIssues,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("CONTAINERS"), m_dockTileTotal,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("CGROUP MEM"), m_dockTileCgMem,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_dockerTable = new QTreeWidget(view);
    m_dockerTable->setObjectName(QStringLiteral("ddDockerTable"));
    m_dockerTable->setColumnCount(3);
    m_dockerTable->setHeaderLabels({QStringLiteral("Container"),
                                    QStringLiteral("State"),
                                    QStringLiteral("Status")});
    m_dockerTable->setRootIsDecorated(false);
    m_dockerTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_dockerTable->setFocusPolicy(Qt::NoFocus);
    m_dockerTable->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 3; ++i) {
        m_dockerTable->header()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }

    // --- Docker inventory + running-count history.
    layout->addWidget(makeBanner(QStringLiteral("DOCKER"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("CONTAINERS"), m_dockerTable, view), 2);

    // --- Running-count history (Syspeek-own counter).
    m_chartDocker = new TimeseriesChart(view);
    m_chartDocker->setUnit(QStringLiteral("Count"));
    m_chartDocker->setSeriesName(QStringLiteral("syspeek.docker.running"));

    layout->addWidget(makeBanner(QStringLiteral("RUNNING OVER TIME"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("running containers"),
                                    m_chartDocker, view));

    // --- Docker diagnosis (state text already carries the summary).
    m_docker = new QLabel(QStringLiteral("--"), view);
    m_docker->setObjectName(QStringLiteral("ddDocker"));
    m_docker->setFont(monoFont(11));
    m_docker->setWordWrap(true);

    layout->addWidget(makeBanner(QStringLiteral("DIAGNOSIS"),
        ui_theme::warn(), view));
    layout->addWidget(makeCard(QStringLiteral("ISSUES"), m_docker, view), 1);

    // --- Focused-app cgroup memory limits (kept card from the old page).
    m_cgroup = new QLabel(QStringLiteral("--"), view);
    m_cgroup->setObjectName(QStringLiteral("ddCgroup"));
    m_cgroup->setFont(monoFont(11));
    m_cgroup->setWordWrap(true);

    layout->addWidget(makeBanner(QStringLiteral("CGROUP"),
        ui_theme::banner(), view));
    layout->addWidget(makeCard(QStringLiteral("CGROUP"), m_cgroup, view), 1);

    m_docker->setMinimumWidth(1);
    m_cgroup->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildKubernetesView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Cluster tiles; cost per month rides the OpenCost model (M17).
    m_kubeTilePods = new QLabel(QStringLiteral("--"), view);
    m_kubeTilePods->setObjectName(QStringLiteral("ddKubeTilePods"));
    m_kubeTileIssues = new QLabel(QStringLiteral("--"), view);
    m_kubeTileIssues->setObjectName(QStringLiteral("ddKubeTileIssues"));
    m_kubeTileDeploys = new QLabel(QStringLiteral("--"), view);
    m_kubeTileDeploys->setObjectName(QStringLiteral("ddKubeTileDeploys"));
    m_kubeTileCost = new QLabel(QStringLiteral("--"), view);
    m_kubeTileCost->setObjectName(QStringLiteral("ddKubeTileCost"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("PODS"), m_kubeTilePods,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("ISSUES"), m_kubeTileIssues,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("DEPLOYMENTS"),
                              m_kubeTileDeploys, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("COST / MONTH"), m_kubeTileCost,
                              nullptr, view), 1);
    layout->addLayout(tiles);

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

    m_kubeSummary = new QLabel(QStringLiteral("--"), view);
    m_kubeSummary->setObjectName(QStringLiteral("ddKubeSummary"));
    m_kubeSummary->setFont(monoFont(12, true));

    layout->addWidget(makeBanner(QStringLiteral("WORKLOADS"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("CLUSTER"), m_kubeSummary, view));

    auto *tables = new QHBoxLayout();
    tables->setSpacing(14);
    tables->addWidget(makeCard(QStringLiteral("PODS"), m_kubePods, view), 1);
    tables->addWidget(
        makeCard(QStringLiteral("DEPLOYMENTS"), m_kubeDeploys, view), 1);
    layout->addLayout(tables, 2);

    // --- Rail-count history (Syspeek-own counter).
    m_chartKube = new TimeseriesChart(view);
    m_chartKube->setUnit(QStringLiteral("Count"));
    m_chartKube->setSeriesName(QStringLiteral("syspeek.kube.pods"));

    layout->addWidget(makeBanner(QStringLiteral("PODS OVER TIME"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("pods on cluster"),
                                    m_chartKube, view));

    m_nodes = new QLabel(QStringLiteral("--"), view);
    m_nodes->setObjectName(QStringLiteral("ddNodes"));
    m_nodes->setFont(monoFont(10));
    m_nodes->setWordWrap(false);

    m_kubeEvents = new QLabel(QStringLiteral("--"), view);
    m_kubeEvents->setObjectName(QStringLiteral("ddKubeEvents"));
    m_kubeEvents->setFont(monoFont(10));
    m_kubeEvents->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("CLUSTER HEALTH"),
        ui_theme::warn(), view));
    auto *healthRow = new QHBoxLayout();
    healthRow->setSpacing(14);
    healthRow->addWidget(makeCard(QStringLiteral("NODES"), m_nodes, view), 1);
    healthRow->addWidget(
        makeCard(QStringLiteral("EVENTS (warnings)"), m_kubeEvents, view), 1);
    layout->addLayout(healthRow, 1);

    m_kubeDiagnosis = new QLabel(QStringLiteral("--"), view);
    m_kubeDiagnosis->setObjectName(QStringLiteral("ddKubeDiagnosis"));
    m_kubeDiagnosis->setFont(monoFont(10));
    m_kubeDiagnosis->setWordWrap(false);

    m_kubeTimeline = new QLabel(QStringLiteral("--"), view);
    m_kubeTimeline->setObjectName(QStringLiteral("ddKubeTimeline"));
    m_kubeTimeline->setFont(monoFont(10));
    m_kubeTimeline->setWordWrap(false);

    auto *insight = new QHBoxLayout();
    insight->setSpacing(14);
    insight->addWidget(
        makeCard(QStringLiteral("DIAGNOSIS (why broken)"), m_kubeDiagnosis, view), 1);
    insight->addWidget(
        makeCard(QStringLiteral("CHANGE TIMELINE"), m_kubeTimeline, view), 1);
    layout->addLayout(insight, 1);

    m_deprecations = new QLabel(QStringLiteral("--"), view);
    m_deprecations->setObjectName(QStringLiteral("ddDeprecations"));
    m_deprecations->setFont(monoFont(10));
    m_deprecations->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("DEPRECATED / REMOVED APIs"), m_deprecations, view), 1);

    for (QLabel *label : {m_nodes, m_kubeEvents, m_kubeDiagnosis,
                          m_kubeTimeline, m_deprecations}) {
        label->setMinimumWidth(1);
    }

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildServicesView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Unit health tiles; failed counts colored red while > 0.
    m_svcTileRunning = new QLabel(QStringLiteral("--"), view);
    m_svcTileRunning->setObjectName(QStringLiteral("ddSvcTileRunning"));
    m_svcTileFailed = new QLabel(QStringLiteral("--"), view);
    m_svcTileFailed->setObjectName(QStringLiteral("ddSvcTileFailed"));
    m_svcTileUnits = new QLabel(QStringLiteral("--"), view);
    m_svcTileUnits->setObjectName(QStringLiteral("ddSvcTileUnits"));
    m_svcTileChanges = new QLabel(QStringLiteral("--"), view);
    m_svcTileChanges->setObjectName(QStringLiteral("ddSvcTileChanges"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("RUNNING"), m_svcTileRunning,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("FAILED"), m_svcTileFailed,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("UNITS"), m_svcTileUnits,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("CHANGES"), m_svcTileChanges,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_servicesSummary = new QLabel(QStringLiteral("--"), view);
    m_servicesSummary->setObjectName(QStringLiteral("ddServicesSummary"));
    m_servicesSummary->setFont(monoFont(12, true));

    m_services = new QLabel(QStringLiteral("--"), view);
    m_services->setObjectName(QStringLiteral("ddServices"));
    m_services->setFont(monoFont(10));
    m_services->setWordWrap(false);

    m_servicesChanges = new QLabel(QStringLiteral("--"), view);
    m_servicesChanges->setObjectName(QStringLiteral("ddServicesChanges"));
    m_servicesChanges->setFont(monoFont(10));
    m_servicesChanges->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("SYSTEM SERVICES"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("SYSTEMD"), m_servicesSummary, view));
    layout->addWidget(
        makeCard(QStringLiteral("SERVICES"), m_services, view), 2);

    layout->addWidget(makeBanner(QStringLiteral("CHANGE TIMELINE"),
        ui_theme::warn(), view));
    layout->addWidget(
        makeCard(QStringLiteral("CHANGES (restarts)"), m_servicesChanges, view), 1);

    m_services->setMinimumWidth(1);
    m_servicesChanges->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildDependenciesView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Library tiles: the SBOM (M23) resolves deps to packages.
    m_depTileLibs = new QLabel(QStringLiteral("--"), view);
    m_depTileLibs->setObjectName(QStringLiteral("ddDepTileLibs"));
    m_depTilePkg = new QLabel(QStringLiteral("--"), view);
    m_depTilePkg->setObjectName(QStringLiteral("ddDepTilePkg"));
    m_depTileUnmapped = new QLabel(QStringLiteral("--"), view);
    m_depTileUnmapped->setObjectName(QStringLiteral("ddDepTileUnmapped"));
    m_depTileRatio = new QLabel(QStringLiteral("--"), view);
    m_depTileRatio->setObjectName(QStringLiteral("ddDepTileRatio"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("LIBRARIES"),
                              m_depTileLibs, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("PACKAGES"), m_depTilePkg,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("UNMAPPED"),
                              m_depTileUnmapped, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("RESOLVED"), m_depTileRatio,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_deps = new QLabel(QStringLiteral("--"), view);
    m_deps->setObjectName(QStringLiteral("ddDeps"));
    m_deps->setFont(monoFont(10));
    m_deps->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("SHARED LIBRARIES"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("SHARED LIBRARIES (focused process)"), m_deps, view),
        1);

    m_deps->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildEndpointsView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Exposure tiles (M12 model: local vs public + db detection).
    m_epTileTotal = new QLabel(QStringLiteral("--"), view);
    m_epTileTotal->setObjectName(QStringLiteral("ddEpTileTotal"));
    m_epTilePublic = new QLabel(QStringLiteral("--"), view);
    m_epTilePublic->setObjectName(QStringLiteral("ddEpTilePublic"));
    m_epTileLocal = new QLabel(QStringLiteral("--"), view);
    m_epTileLocal->setObjectName(QStringLiteral("ddEpTileLocal"));
    m_epTileDb = new QLabel(QStringLiteral("--"), view);
    m_epTileDb->setObjectName(QStringLiteral("ddEpTileDb"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("ENDPOINTS"), m_epTileTotal,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("PUBLIC"), m_epTilePublic,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("LOCAL"), m_epTileLocal,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("DATABASES"), m_epTileDb,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_endpoints = new QLabel(QStringLiteral("--"), view);
    m_endpoints->setObjectName(QStringLiteral("ddEndpoints"));
    m_endpoints->setFont(monoFont(10));
    m_endpoints->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("LISTENING ENDPOINTS"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("LISTENING ENDPOINTS (host)"), m_endpoints, view),
        1);

    m_endpoints->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildSyntheticView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Probe tiles; down count red while > 0.
    m_synTileChecks = new QLabel(QStringLiteral("--"), view);
    m_synTileChecks->setObjectName(QStringLiteral("ddSynTileChecks"));
    m_synTileUp = new QLabel(QStringLiteral("--"), view);
    m_synTileUp->setObjectName(QStringLiteral("ddSynTileUp"));
    m_synTileDown = new QLabel(QStringLiteral("--"), view);
    m_synTileDown->setObjectName(QStringLiteral("ddSynTileDown"));
    m_synTileAvg = new QLabel(QStringLiteral("--"), view);
    m_synTileAvg->setObjectName(QStringLiteral("ddSynTileAvg"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("CHECKS"), m_synTileChecks,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("UP"), m_synTileUp,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("DOWN"), m_synTileDown,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("AVG MS"), m_synTileAvg,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_syntheticSummary = new QLabel(QStringLiteral("--"), view);
    m_syntheticSummary->setObjectName(QStringLiteral("ddSyntheticSummary"));
    m_syntheticSummary->setFont(monoFont(12, true));

    m_synthetic = new QLabel(QStringLiteral("--"), view);
    m_synthetic->setObjectName(QStringLiteral("ddSynthetic"));
    m_synthetic->setFont(monoFont(10));
    m_synthetic->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("SYNTHETIC CHECKS"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("SYNTHETIC"), m_syntheticSummary, view));

    // --- Latency history (probe avg; Syspeek-own series - the agent's
    // synthetic tests are SaaS-scheduled, no shipped metric name).
    m_chartSynthetic = new TimeseriesChart(view);
    m_chartSynthetic->setUnit(QStringLiteral("Milliseconds"));
    m_chartSynthetic->setSeriesName(QStringLiteral("syspeek.synthetic.duration"));

    layout->addWidget(makeBanner(QStringLiteral("LATENCY"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("probe avg duration"),
                                    m_chartSynthetic, view));

    layout->addWidget(makeBanner(QStringLiteral("HTTP CHECKS"),
        ui_theme::warn(), view));
    layout->addWidget(makeCard(QStringLiteral("HTTP CHECKS"), m_synthetic, view), 2);

    m_synthetic->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildInstrumentationView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Agent detection tiles (M12/M33 model): dd / otel / other.
    m_insTileAgents = new QLabel(QStringLiteral("--"), view);
    m_insTileAgents->setObjectName(QStringLiteral("ddInsTileAgents"));
    m_insTileDd = new QLabel(QStringLiteral("--"), view);
    m_insTileDd->setObjectName(QStringLiteral("ddInsTileDd"));
    m_insTileOtel = new QLabel(QStringLiteral("--"), view);
    m_insTileOtel->setObjectName(QStringLiteral("ddInsTileOtel"));
    m_insTileOther = new QLabel(QStringLiteral("--"), view);
    m_insTileOther->setObjectName(QStringLiteral("ddInsTileOther"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("AGENTS"), m_insTileAgents,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("DATADOG"), m_insTileDd,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("OTEL"), m_insTileOtel,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("OTHER"), m_insTileOther,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_instrumentation = new QLabel(QStringLiteral("--"), view);
    m_instrumentation->setObjectName(QStringLiteral("ddInstrumentation"));
    m_instrumentation->setFont(monoFont(11));

    layout->addWidget(makeBanner(QStringLiteral("INSTRUMENTATION"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("INSTRUMENTATION (focused app)"),
                 m_instrumentation, view), 1);

    m_instrumentation->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildDatabasesView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Postgres-health tiles (M30/M40 model).
    m_dbTileCache = new QLabel(QStringLiteral("--"), view);
    m_dbTileCache->setObjectName(QStringLiteral("ddDbTileCache"));
    m_dbTileCommits = new QLabel(QStringLiteral("--"), view);
    m_dbTileCommits->setObjectName(QStringLiteral("ddDbTileCommits"));
    m_dbTileRollbacks = new QLabel(QStringLiteral("--"), view);
    m_dbTileRollbacks->setObjectName(QStringLiteral("ddDbTileRollbacks"));
    m_dbTileDeadlocks = new QLabel(QStringLiteral("--"), view);
    m_dbTileDeadlocks->setObjectName(QStringLiteral("ddDbTileDeadlocks"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("CACHE HIT"), m_dbTileCache,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("COMMITS /S"), m_dbTileCommits,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("ROLLBACKS"), m_dbTileRollbacks,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("DEADLOCKS"), m_dbTileDeadlocks,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_dbmSummary = new QLabel(QStringLiteral("--"), view);
    m_dbmSummary->setObjectName(QStringLiteral("ddDbmSummary"));
    m_dbmSummary->setFont(monoFont(13, true));

    m_dbm = new QLabel(QStringLiteral("--"), view);
    m_dbm->setObjectName(QStringLiteral("ddDbm"));
    m_dbm->setFont(monoFont(10));
    m_dbm->setWordWrap(false);

    m_dbmStatements = new QLabel(QStringLiteral("--"), view);
    m_dbmStatements->setObjectName(QStringLiteral("ddDbmStatements"));
    m_dbmStatements->setFont(monoFont(10));
    m_dbmStatements->setWordWrap(false);

    m_databases = new QLabel(QStringLiteral("--"), view);
    m_databases->setObjectName(QStringLiteral("ddDatabases"));
    m_databases->setFont(monoFont(10));
    m_databases->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("DATABASE MONITORING"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("DATABASE MONITORING (PostgreSQL)"),
                 m_dbmSummary, view));
    layout->addWidget(makeCard(QStringLiteral("DBM"), m_dbm, view), 1);

    // --- Transaction-rate history; series names are the shipped
    // postgres integration's (agent-bundle metadata on record).
    m_chartDb = new TimeseriesChart(view);
    m_chartDb->setUnit(QStringLiteral("Per Second"));
    m_chartDb->setSeriesName(QStringLiteral("postgresql.commits"));
    m_chartDb->setSeriesNameB(QStringLiteral("postgresql.rollbacks"));

    layout->addWidget(makeBanner(QStringLiteral("TRANSACTION THROUGHPUT"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("commits vs rollbacks"),
                                    m_chartDb, view));

    layout->addWidget(makeBanner(QStringLiteral("TOP QUERIES"),
        ui_theme::warn(), view));
    layout->addWidget(
        makeCard(QStringLiteral("TOP QUERIES (pg_stat_statements)"),
                 m_dbmStatements, view), 1);
    layout->addWidget(
        makeCard(QStringLiteral("APP DATABASE CONNECTIONS"), m_databases, view),
        1);

    m_dbm->setMinimumWidth(1);
    m_dbmStatements->setMinimumWidth(1);
    m_databases->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildLogsView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Journal-flow tiles (per-window counts from the sample).
    m_logTileEntries = new QLabel(QStringLiteral("--"), view);
    m_logTileEntries->setObjectName(QStringLiteral("ddLogTileEntries"));
    m_logTileErrors = new QLabel(QStringLiteral("--"), view);
    m_logTileErrors->setObjectName(QStringLiteral("ddLogTileErrors"));
    m_logTileLevels = new QLabel(QStringLiteral("--"), view);
    m_logTileLevels->setObjectName(QStringLiteral("ddLogTileLevels"));
    m_logTilePipeline = new QLabel(QStringLiteral("--"), view);
    m_logTilePipeline->setObjectName(QStringLiteral("ddLogTilePipeline"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("LOG LINES"), m_logTileEntries,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("ERRORS"), m_logTileErrors,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("LEVELS"), m_logTileLevels,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("PARSE"), m_logTilePipeline,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_logsSummary = new QLabel(QStringLiteral("--"), view);
    m_logsSummary->setObjectName(QStringLiteral("ddLogsSummary"));
    m_logsSummary->setFont(monoFont(12, true));

    layout->addWidget(makeBanner(QStringLiteral("JOURNAL"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("JOURNALD"), m_logsSummary, view));

    layout->addWidget(makeBanner(QStringLiteral("LOG PIPELINE"),
        ui_theme::warn(), view));

    m_logPipeline = new QLabel(QStringLiteral("--"), view);
    m_logPipeline->setObjectName(QStringLiteral("ddLogPipeline"));
    m_logPipeline->setFont(monoFont(10));
    m_logPipeline->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("LOG PIPELINE (parsed fields)"), m_logPipeline, view), 1);

    // --- Per-tick line volume (Syspeek-own counter).
    m_chartLog = new TimeseriesChart(view);
    m_chartLog->setUnit(QStringLiteral("Count"));
    m_chartLog->setSeriesName(QStringLiteral("syspeek.journal.lines"));

    layout->addWidget(makeBanner(QStringLiteral("LOG VOLUME"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("lines per journal sample"),
                                    m_chartLog, view));

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
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Error-group tiles.
    m_errTileGroups = new QLabel(QStringLiteral("--"), view);
    m_errTileGroups->setObjectName(QStringLiteral("ddErrTileGroups"));
    m_errTileCount = new QLabel(QStringLiteral("--"), view);
    m_errTileCount->setObjectName(QStringLiteral("ddErrTileCount"));
    m_errTileWorst = new QLabel(QStringLiteral("--"), view);
    m_errTileWorst->setObjectName(QStringLiteral("ddErrTileWorst"));
    m_errTileRate = new QLabel(QStringLiteral("--"), view);
    m_errTileRate->setObjectName(QStringLiteral("ddErrTileRate"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("GROUPS"), m_errTileGroups,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("ERRORS"), m_errTileCount,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("TOP GROUP"), m_errTileWorst,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("WORST PRI"), m_errTileRate,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_errors = new QLabel(QStringLiteral("--"), view);
    m_errors->setObjectName(QStringLiteral("ddErrors"));
    m_errors->setFont(monoFont(10));
    m_errors->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("ERRORS AND WORSE"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("ERROR GROUPS"), m_errors, view), 1);

    m_errors->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
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
    general += QStringLiteral("prometheus&nbsp;%1<br/>")
        .arg(m_settings.metricsLine.isEmpty()
                 ? QStringLiteral("--")
                 : m_settings.metricsLine);
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
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- I/O fleet tiles; PSI tile is n/a off Linux like the card.
    m_diskTileRead = new QLabel(QStringLiteral("--"), view);
    m_diskTileRead->setObjectName(QStringLiteral("ddDiskTileRead"));
    m_diskTileWrite = new QLabel(QStringLiteral("--"), view);
    m_diskTileWrite->setObjectName(QStringLiteral("ddDiskTileWrite"));
    m_diskTileUtil = new QLabel(QStringLiteral("--"), view);
    m_diskTileUtil->setObjectName(QStringLiteral("ddDiskTileUtil"));
    m_diskTilePsi = new QLabel(QStringLiteral("--"), view);
    m_diskTilePsi->setObjectName(QStringLiteral("ddDiskTilePsi"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("READ KB/S"), m_diskTileRead,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("WRITE KB/S"), m_diskTileWrite,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("UTIL MAX"), m_diskTileUtil,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("PSI SOME"), m_diskTilePsi,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_diskSummary = new QLabel(QStringLiteral("--"), view);
    m_diskSummary->setObjectName(QStringLiteral("ddDiskSummary"));
    m_diskSummary->setFont(monoFont(10));
    layout->addWidget(
        makeCard(QStringLiteral("DISK I/O"), m_diskSummary, view));

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

    layout->addWidget(makeBanner(QStringLiteral("DEVICES"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("DEVICES"), m_disks, view), 2);

    // --- Throughput history (DD agent vocabulary: system.io.rkb_s).
    m_chartIoRw = new TimeseriesChart(view);
    m_chartIoRw->setUnit(QStringLiteral("kB/s"));
    m_chartIoRw->setSeriesName(QStringLiteral("system.io.rkb_s"));
    m_chartIoRw->setSeriesNameB(QStringLiteral("system.io.wkb_s"));

    layout->addWidget(makeBanner(QStringLiteral("READ / WRITE"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("disk throughput"),
                                    m_chartIoRw, view));

    // --- Busy-ness history (max %util across devices).
    m_chartIoUtil = new TimeseriesChart(view);
    m_chartIoUtil->setUnit(QStringLiteral("Percent"));
    m_chartIoUtil->setSeriesName(QStringLiteral("system.io.util"));

    layout->addWidget(makeBanner(QStringLiteral("I/O UTILISATION"),
        ui_theme::warn(), view));
    layout->addWidget(makeChartCard(QStringLiteral(
        "busiest device %util"), m_chartIoUtil, view));

    m_diskPressure = new QLabel(QStringLiteral("--"), view);
    m_diskPressure->setObjectName(QStringLiteral("ddDiskPressure"));
    m_diskPressure->setFont(monoFont(10));
    m_diskPressure->setWordWrap(false);
    layout->addWidget(
        makeCard(QStringLiteral("I/O PRESSURE (PSI)"), m_diskPressure, view), 1);

    m_diskPressure->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildHostView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Live vitals tiles (the inventory cards stay below).
    m_hostTileCpu = new QLabel(QStringLiteral("--"), view);
    m_hostTileCpu->setObjectName(QStringLiteral("ddHostTileCpu"));
    m_hostTileLoad = new QLabel(QStringLiteral("--"), view);
    m_hostTileLoad->setObjectName(QStringLiteral("ddHostTileLoad"));
    m_hostTileAvail = new QLabel(QStringLiteral("--"), view);
    m_hostTileAvail->setObjectName(QStringLiteral("ddHostTileAvail"));
    m_hostTileDisk = new QLabel(QStringLiteral("--"), view);
    m_hostTileDisk->setObjectName(QStringLiteral("ddHostTileDisk"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("HOST CPU"), m_hostTileCpu,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("LOAD 1M"), m_hostTileLoad,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("MEM AVAIL"), m_hostTileAvail,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("DISK FREE"), m_hostTileDisk,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_hostInfoSummary = new QLabel(QStringLiteral("--"), view);
    m_hostInfoSummary->setObjectName(QStringLiteral("ddHostInfoSummary"));
    m_hostInfoSummary->setFont(monoFont(13, true));

    layout->addWidget(makeBanner(QStringLiteral("HOST INVENTORY"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("HOST INVENTORY"), m_hostInfoSummary, view), 1);

    // --- Load history (Syspeek-own counter; no honest hostLoad analogue
    // exists beyond it on Windows, which stays "--").
    m_chartHostPage = new TimeseriesChart(view);
    m_chartHostPage->setUnit(QStringLiteral("Load"));
    m_chartHostPage->setSeriesName(QStringLiteral("syspeek.host.load1"));

    layout->addWidget(makeBanner(QStringLiteral("LOAD OVER TIME"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("load average (1m)"),
                                    m_chartHostPage, view));

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

    auto *row3 = new QHBoxLayout;
    row3->setSpacing(10);
    m_kernel = new QLabel(QStringLiteral("--"), view);
    m_kernel->setObjectName(QStringLiteral("ddKernel"));
    m_kernel->setFont(monoFont(10));
    m_kernel->setWordWrap(false);
    layout->addWidget(makeBanner(QStringLiteral("KERNEL & LIMITS"),
        ui_theme::warn(), view));
    row3->addWidget(
        makeCard(QStringLiteral("KERNEL & LIMITS"), m_kernel, view), 1);
    layout->addLayout(row3, 1);

    for (QLabel *label : {m_hostPlatform, m_hostCpuInfo, m_hostFilesystems,
                          m_hostNetwork, m_kernel}) {
        label->setMinimumWidth(1);
    }

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildSecurityView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Posture + threat tiles; setters feed their own tile below.
    m_secTileExposure = new QLabel(QStringLiteral("--"), view);
    m_secTileExposure->setObjectName(QStringLiteral("ddSecTileExposure"));
    m_secTileFindings = new QLabel(QStringLiteral("--"), view);
    m_secTileFindings->setObjectName(QStringLiteral("ddSecTileFindings"));
    m_secTileThreats = new QLabel(QStringLiteral("--"), view);
    m_secTileThreats->setObjectName(QStringLiteral("ddSecTileThreats"));
    m_secTileFim = new QLabel(QStringLiteral("--"), view);
    m_secTileFim->setObjectName(QStringLiteral("ddSecTileFim"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("EXPOSURE"), m_secTileExposure,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("FINDINGS"), m_secTileFindings,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("THREATS"), m_secTileThreats,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("FILE CHANGES"), m_secTileFim,
                              nullptr, view), 1);
    layout->addLayout(tiles);

    m_securitySummary = new QLabel(QStringLiteral("--"), view);
    m_securitySummary->setObjectName(QStringLiteral("ddSecuritySummary"));
    m_securitySummary->setFont(monoFont(14, true));

    m_security = new QLabel(QStringLiteral("--"), view);
    m_security->setObjectName(QStringLiteral("ddSecurity"));
    m_security->setFont(monoFont(10));
    m_security->setWordWrap(false);

    m_threats = new QLabel(QStringLiteral("--"), view);
    m_threats->setObjectName(QStringLiteral("ddThreats"));
    m_threats->setFont(monoFont(10));
    m_threats->setWordWrap(false);

    m_fimSummary = new QLabel(QStringLiteral("--"), view);
    m_fimSummary->setObjectName(QStringLiteral("ddFimSummary"));
    m_fimSummary->setFont(monoFont(13, true));

    m_fim = new QLabel(QStringLiteral("--"), view);
    m_fim->setObjectName(QStringLiteral("ddFim"));
    m_fim->setFont(monoFont(10));
    m_fim->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("SANDBOX EXPOSURE"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("SANDBOX EXPOSURE"), m_securitySummary, view));
    layout->addWidget(
        makeCard(QStringLiteral("HARDENING FINDINGS"), m_security, view), 1);

    layout->addWidget(makeBanner(QStringLiteral("THREATS"),
        ui_theme::warn(), view));
    layout->addWidget(
        makeCard(QStringLiteral("THREATS (log-based SIEM)"), m_threats, view), 1);

    layout->addWidget(makeBanner(QStringLiteral("FILE INTEGRITY"),
        ui_theme::warn(), view));
    layout->addWidget(
        makeCard(QStringLiteral("FILE INTEGRITY (FIM)"), m_fimSummary, view));
    layout->addWidget(makeCard(QStringLiteral("FILE CHANGES"), m_fim, view), 1);

    m_security->setMinimumWidth(1);
    m_threats->setMinimumWidth(1);
    m_fim->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildTracesView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- Span-flow tiles.
    m_traceTileTraces = new QLabel(QStringLiteral("--"), view);
    m_traceTileTraces->setObjectName(QStringLiteral("ddTraceTileTraces"));
    m_traceTileSpans = new QLabel(QStringLiteral("--"), view);
    m_traceTileSpans->setObjectName(QStringLiteral("ddTraceTileSpans"));
    m_traceTileErrors = new QLabel(QStringLiteral("--"), view);
    m_traceTileErrors->setObjectName(QStringLiteral("ddTraceTileErrors"));
    m_traceTileServices = new QLabel(QStringLiteral("--"), view);
    m_traceTileServices->setObjectName(QStringLiteral("ddTraceTileServices"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("TRACES"), m_traceTileTraces,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("SPANS"), m_traceTileSpans,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("ERRORS"), m_traceTileErrors,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("SERVICES"),
                              m_traceTileServices, nullptr, view), 1);
    layout->addLayout(tiles);

    m_traceSummary = new QLabel(QStringLiteral("--"), view);
    m_traceSummary->setObjectName(QStringLiteral("ddTraceSummary"));
    m_traceSummary->setFont(monoFont(13, true));

    m_traceMap = new QLabel(QStringLiteral("--"), view);
    m_traceMap->setObjectName(QStringLiteral("ddTraceMap"));
    m_traceMap->setFont(monoFont(10));
    m_traceMap->setWordWrap(false);

    m_traceServices = new QLabel(QStringLiteral("--"), view);
    m_traceServices->setObjectName(QStringLiteral("ddTraceServices"));
    m_traceServices->setFont(monoFont(10));
    m_traceServices->setWordWrap(false);

    m_traceRecent = new QLabel(QStringLiteral("--"), view);
    m_traceRecent->setObjectName(QStringLiteral("ddTraceRecent"));
    m_traceRecent->setFont(monoFont(10));
    m_traceRecent->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("APM / TRACES"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("APM (OTLP :4318)"), m_traceSummary, view));

    // --- Latency history (mean service latency per sample; spans are
    // forwarded host-side - per-app latency is not an agent metric).
    m_chartTrace = new TimeseriesChart(view);
    m_chartTrace->setUnit(QStringLiteral("Milliseconds"));
    m_chartTrace->setSeriesName(QStringLiteral("syspeek.trace.latency"));

    layout->addWidget(makeBanner(QStringLiteral("LATENCY"),
        ui_theme::banner(), view));
    layout->addWidget(makeChartCard(QStringLiteral("mean service latency"),
                                    m_chartTrace, view));

    layout->addWidget(makeBanner(QStringLiteral("SERVICES"),
        ui_theme::warn(), view));
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    row->addWidget(makeCard(QStringLiteral("SERVICE MAP"), m_traceMap, view), 1);
    row->addWidget(
        makeCard(QStringLiteral("SERVICE LATENCY"), m_traceServices, view), 1);
    layout->addLayout(row, 1);
    layout->addWidget(
        makeCard(QStringLiteral("RECENT TRACES"), m_traceRecent, view), 1);

    m_traceMap->setMinimumWidth(1);
    m_traceServices->setMinimumWidth(1);
    m_traceRecent->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

QWidget *DashboardPanel::buildSoftwareView() {
    auto *view = new QWidget(this);
    auto *content = new QWidget(view);
    content->setObjectName(QStringLiteral("ddScrollContent"));
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // --- SBOM tiles (M23 model: entries resolve focused-app files).
    m_sweTilePkgs = new QLabel(QStringLiteral("--"), view);
    m_sweTilePkgs->setObjectName(QStringLiteral("ddSweTilePkgs"));
    m_sweTileLibs = new QLabel(QStringLiteral("--"), view);
    m_sweTileLibs->setObjectName(QStringLiteral("ddSweTileLibs"));
    m_sweTileResolved = new QLabel(QStringLiteral("--"), view);
    m_sweTileResolved->setObjectName(QStringLiteral("ddSweTileResolved"));
    m_sweTileFams = new QLabel(QStringLiteral("--"), view);
    m_sweTileFams->setObjectName(QStringLiteral("ddSweTileFams"));

    auto *tiles = new QHBoxLayout();
    tiles->setSpacing(10);
    tiles->addWidget(makeTile(QStringLiteral("PACKAGES"), m_sweTilePkgs,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("LIBRARIES"), m_sweTileLibs,
                              nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("RESOLVED"),
                              m_sweTileResolved, nullptr, view), 1);
    tiles->addWidget(makeTile(QStringLiteral("PURL FAMILIES"),
                              m_sweTileFams, nullptr, view), 1);
    layout->addLayout(tiles);

    m_softwareSummary = new QLabel(QStringLiteral("--"), view);
    m_softwareSummary->setObjectName(QStringLiteral("ddSoftwareSummary"));
    m_softwareSummary->setFont(monoFont(13, true));

    m_software = new QLabel(QStringLiteral("--"), view);
    m_software->setObjectName(QStringLiteral("ddSoftware"));
    m_software->setFont(monoFont(10));
    m_software->setWordWrap(false);

    layout->addWidget(makeBanner(QStringLiteral("SBOM / SOFTWARE"),
        ui_theme::accent(), view));
    layout->addWidget(
        makeCard(QStringLiteral("SBOM (focused app)"), m_softwareSummary, view));
    layout->addWidget(
        makeCard(QStringLiteral("PACKAGES"), m_software, view), 1);

    m_software->setMinimumWidth(1);

    auto *scroll = new QScrollArea(view);
    scroll->setObjectName(QStringLiteral("ddScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setWidget(content);
    content->setAttribute(Qt::WA_StyledBackground, true);
    content->setStyleSheet(QStringLiteral(
        "#ddScrollContent { background-color: %1; }").arg(ui_theme::bg()));
    auto *outer = new QVBoxLayout(view);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    outer->addWidget(scroll);
    return view;
}

void DashboardPanel::setSnapshot(const DisplaySnapshot &snapshot) {
    m_appValue->setText(snapshot.appName);
    m_pidValue->setText(QStringLiteral("pid %1").arg(snapshot.pid));
    m_cpuValue->setText(snapshot.cpu);
    m_memValue->setText(snapshot.memory);
    m_procCount->setText(QString::number(snapshot.processes.size()));

    // Processes-page fleet totals (M62): sum the per-row resource axes
    // and keep a CPU-vs-load history for the page chart.
    qint64 procsThreads = 0;
    qint64 procsFds = 0;
    double procsCpu = 0.0;
    for (const ProcessRow &row : snapshot.processes) {
        procsThreads += row.threads;
        if (row.fds >= 0) {
            procsFds += row.fds;
        }
        procsCpu += qMax(0.0, row.cpuPercent);
    }
    m_procTileProcs->setText(QString::number(snapshot.processes.size()));
    m_procTileThreads->setText(QString::number(procsThreads));
    m_procTileFds->setText(QString::number(procsFds));
    m_procTileLoad->setText(QString::number(snapshot.hostLoad1, 'f', 2));
    if (!snapshot.processes.isEmpty() && m_chartProcTotals != nullptr) {
        m_chartProcTotals->push(qBound<double>(0.0, procsCpu, 100000.0));
        m_chartProcTotals->pushB(qMax(0.0, snapshot.hostLoad1));
    }

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
        if (m_cpuSpark != nullptr) {
            m_cpuSpark->setValues(m_cpuStore.values());
        }
    }
    if (snapshot.rssKb > 0) {
        m_memStore.push(static_cast<double>(snapshot.rssKb));
        if (m_memSpark != nullptr) {
            m_memSpark->setValues(m_memStore.values());
        }
    }

    // Datadog-console chart feeds: one ring per metric; the widgets
    // gate paused pushes themselves. Cadence EWMA drives the time chip
    // and the delta labels.
    const qint64 tickMs = QDateTime::currentMSecsSinceEpoch();
    if (m_lastTickMs > 0) {
        const double gap = static_cast<double>(tickMs - m_lastTickMs);
        if (gap > 10.0 && gap < 60000.0) {
            m_cadenceMs = (m_cadenceMs <= 0.0)
                ? gap
                : (1.0 - 0.15) * m_cadenceMs + 0.15 * gap;
        }
    }
    m_lastTickMs = tickMs;

    m_procStore.push(static_cast<double>(snapshot.processes.size()));
    if (m_chartProc != nullptr) {
        m_chartProc->push(static_cast<double>(snapshot.processes.size()));
    }
    if (snapshot.cpuValid && m_chartCpu != nullptr) {
        m_chartCpu->push(snapshot.cpuPercent);
    }
    if (snapshot.rssKb > 0 && m_chartMem != nullptr) {
        m_chartMem->push(snapshot.rssKb / 1024.0);
    }
    if (snapshot.disksAvailable) {
        double readKb = 0.0;
        double writeKb = 0.0;
        double utilMax = 0.0;
        for (const DiskRow &disk : snapshot.disks) {
            readKb += disk.readKbPerSec;
            writeKb += disk.writeKbPerSec;
            utilMax = std::max(utilMax, disk.utilPercent);
        }
        m_diskReadStore.push(readKb);
        m_diskWriteStore.push(writeKb);
        m_diskUtilStore.push(utilMax);
        if (m_chartDisk != nullptr) {
            m_chartDisk->push(readKb);
            m_chartDisk->pushB(writeKb);
        }
        if (m_chartDiskUtil != nullptr) {
            m_chartDiskUtil->push(utilMax);
        }
        // Disks-page tiles + charts (M61j).
        m_diskTileRead->setText(QString::number(readKb, 'f', 1));
        m_diskTileWrite->setText(QString::number(writeKb, 'f', 1));
        m_diskTileUtil->setText(
            QStringLiteral("%1%").arg(utilMax, 0, 'f', 1));
        if (m_chartIoRw != nullptr) {
            m_chartIoRw->push(readKb);
            m_chartIoRw->pushB(writeKb);
        }
        if (m_chartIoUtil != nullptr) {
            m_chartIoUtil->push(utilMax);
        }
    } else {
        m_diskTileRead->setText(QStringLiteral("--"));
        m_diskTileWrite->setText(QStringLiteral("--"));
        m_diskTileUtil->setText(QStringLiteral("--"));
    }
    if (snapshot.ioPressureAvailable) {
        m_diskTilePsi->setText(
            QStringLiteral("%1%").arg(snapshot.ioPressure.someAvg10, 0, 'f', 1));
    } else {
        m_diskTilePsi->setText(m_osPsi ? QStringLiteral("--")
                                       : QStringLiteral("n/a"));
    }
    if (snapshot.hostAvailable && !snapshot.hostWarming
            && m_chartHostCpu != nullptr) {
        m_chartHostCpu->push(snapshot.hostCpuPercent);
    }
    if (snapshot.hostAvailable && m_chartHostMem != nullptr) {
        m_hostMemStore.push(
            static_cast<double>(snapshot.hostMemAvailableKb) / (1024.0 * 1024.0));
        m_chartHostMem->push(
            static_cast<double>(snapshot.hostMemAvailableKb) / (1024.0 * 1024.0));
    }
    // Host-page tiles + load history (M61k).
    if (snapshot.hostAvailable) {
        m_hostTileCpu->setText(
            QStringLiteral("%1%").arg(snapshot.hostCpuPercent, 0, 'f', 1));
        m_hostTileLoad->setText(QString::number(snapshot.hostLoad1, 'f', 2));
        m_hostTileAvail->setText(
            formatMemoryKb(snapshot.hostMemAvailableKb));
        const double diskTotal =
            static_cast<double>(snapshot.hostDiskTotalBytes);
        m_hostTileDisk->setText(
            diskTotal > 0.0
                ? QStringLiteral("%1%").arg(
                    100.0 * static_cast<double>(snapshot.hostDiskFreeBytes)
                    / diskTotal, 0, 'f', 0)
                : QStringLiteral("--"));
        if (m_chartHostPage != nullptr) {
            m_chartHostPage->push(snapshot.hostLoad1);
        }
    }
    if (snapshot.hostAvailable && m_chartLoad != nullptr) {
        m_loadStore.push(snapshot.hostLoad1);
        m_chartLoad->push(snapshot.hostLoad1);
        m_netRxStore.push(snapshot.hostNetRxPerSec / 1024.0);
        m_netTxStore.push(snapshot.hostNetTxPerSec / 1024.0);
        m_chartNet->push(snapshot.hostNetRxPerSec / 1024.0);
        m_chartNet->pushB(snapshot.hostNetTxPerSec / 1024.0);
    }
    // Network-page tiles + throughput history (M61l).
    int netEstablished = 0;
    for (const ConnectionRow &conn : snapshot.connections) {
        if (conn.state.contains(QLatin1String("ESTABLISHED"))) {
            ++netEstablished;
        }
    }
    m_netTileConns->setText(QString::number(snapshot.connections.size()));
    m_netTileEstab->setText(QString::number(netEstablished));
    if (snapshot.tcpHealthAvailable) {
        m_netTileRetrans->setText(
            QStringLiteral("%1%").arg(snapshot.tcpHealth.retransPercent,
                                      0, 'f', 2));
        m_netTileRetrans->setStyleSheet(
            QStringLiteral("color: %1;").arg(
                snapshot.tcpHealth.retransPercent >= 1.0
                    ? ui_theme::crit()
                    : (snapshot.tcpHealth.retransPercent >= 0.5
                        ? ui_theme::warn()
                        : ui_theme::text())));
    } else {
        m_netTileRetrans->setText(m_osTcpCounters ? QStringLiteral("--")
                                                  : QStringLiteral("n/a"));
    }
    if (snapshot.hostAvailable) {
        m_netTileTx->setText(
            QStringLiteral("%1").arg(snapshot.hostNetTxPerSec / 1024.0, 0, 'f', 1));
        if (m_chartNetPage != nullptr) {
            m_chartNetPage->push(snapshot.hostNetRxPerSec / 1024.0);
            m_chartNetPage->pushB(snapshot.hostNetTxPerSec / 1024.0);
        }
        int rttRows = 0;
        double rttSum = 0.0;
        for (const ConnectionRow &conn2 : snapshot.connections) {
            if (conn2.hasTcpDiag && (conn2.state == QStringLiteral("ESTABLISHED")
                                     || conn2.rttUsec > 0)) {
                rttSum += static_cast<double>(conn2.rttUsec);
                ++rttRows;
            }
        }
        if (m_chartConnRtt != nullptr && rttRows > 0) {
            m_chartConnRtt->push(rttSum / rttRows / 1000.0);
        }
    }

    updateOverviewText(snapshot);

    m_memDetail->setText(QStringLiteral("RSS      %1\nVirtual  %2\nSwap     %3\nShared   %4\noom      %5")
                             .arg(formatMemoryKb(snapshot.rssKb),
                                  formatMemoryKb(snapshot.vmsKb),
                                  formatMemoryKb(snapshot.swapKb),
                                  formatMemoryKb(snapshot.sharedKb),
                                  snapshot.focusOom.score >= 0
                                      ? QStringLiteral("score %1  adj %2")
                                            .arg(snapshot.focusOom.score)
                                            .arg(snapshot.focusOom.scoreAdj)
                                      : QStringLiteral("--")));
    // Memory-page tiles + used/available history (M61i); used is
    // total - available (the snapshot does not split cache yet).
    m_memTileTotal->setText(formatMemoryKb(snapshot.hostMemTotalKb));
    m_memTileAvail->setText(formatMemoryKb(snapshot.hostMemAvailableKb));
    m_memTileUsed->setText(
        formatMemoryKb(snapshot.hostMemTotalKb > snapshot.hostMemAvailableKb
                           ? snapshot.hostMemTotalKb
                                 - snapshot.hostMemAvailableKb
                           : 0));
    m_memTileRss->setText(formatMemoryKb(snapshot.rssKb));
    if (m_chartMemPage != nullptr && snapshot.hostMemTotalKb > 0
        && snapshot.hostMemAvailableKb > 0) {
        m_chartMemPage->push(
            static_cast<double>(snapshot.hostMemTotalKb
                                - snapshot.hostMemAvailableKb)
            / (1024.0 * 1024.0));
        m_chartMemPage->pushB(
            static_cast<double>(snapshot.hostMemAvailableKb)
            / (1024.0 * 1024.0));
    }

    QString net;
    int shown = 0;
    for (const ConnectionRow &conn : snapshot.connections) {
        if (shown >= 200) {
            break;
        }
        QString line = QStringLiteral("%1 %2 -> %3  %4")
                           .arg(conn.proto, conn.local, conn.remote, conn.state);
        if (conn.hasTcpDiag) {
            line += QStringLiteral("\n   rtt %1/%2ms cwnd %3"
                                   " rtx %4/%5 out %6 in %7 (%8)")
                        .arg(conn.rttUsec / 1000.0, 0, 'f', 2)
                        .arg(conn.rttVarUsec / 1000.0, 0, 'f', 1)
                        .arg(conn.cwnd)
                        .arg(conn.retransmits)
                        .arg(conn.totalRetrans)
                        .arg(formatBytesCompact(conn.bytesAcked),
                             formatBytesCompact(conn.bytesReceived),
                             conn.congestion);
            QString markers;
            if (conn.retransDelta > 0) {
                markers += QStringLiteral("rtx+%1").arg(conn.retransDelta);
            }
            if (conn.skDrops > 0) {
                if (!markers.isEmpty()) {
                    markers += QStringLiteral("  ");
                }
                markers +=
                    QStringLiteral("skdrop %1").arg(conn.skDrops);
                if (conn.skdropDelta > 0) {
                    markers += QStringLiteral(" (+%1)").arg(
                        conn.skdropDelta);
                }
            }
            if (!markers.isEmpty()) {
                line += QStringLiteral("\n   %1").arg(markers);
            }
        }
        net += line + QLatin1Char('\n');
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
        if (entry.backlog > 0) {
            line += QStringLiteral("  backlog %1").arg(entry.backlog);
        }
        endpointText += line + QLatin1Char('\n');
        ++epCount;
    }
    m_endpoints->setText(endpointText.isEmpty() ? QStringLiteral("none")
                                                : endpointText);

    // Endpoints-page tiles (M61s): exposure split + db detection.
    {
        int publicCount = 0;
        int localCount = 0;
        int dbCount = 0;
        for (const EndpointRow &entry : snapshot.endpoints) {
            if (entry.exposure == QStringLiteral("public")) {
                ++publicCount;
            } else if (entry.exposure == QStringLiteral("local")) {
                ++localCount;
            }
            if (entry.database) {
                ++dbCount;
            }
        }
        m_epTileTotal->setText(QString::number(snapshot.endpoints.size()));
        m_epTilePublic->setText(QString::number(publicCount));
        m_epTilePublic->setStyleSheet(
            publicCount > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::warn())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        m_epTileLocal->setText(QString::number(localCount));
        m_epTileDb->setText(QString::number(dbCount));
    }

    m_instrumentation->setText(
        snapshot.instrumentation.isEmpty()
            ? QStringLiteral("no agent detected")
            : snapshot.instrumentation.join(QStringLiteral(", ")));

    // Instrumentation-page tiles (M61u): agent presence by family.
    if (snapshot.instrumentation.isEmpty()) {
        m_insTileAgents->setText(QStringLiteral("0"));
        m_insTileDd->setText(QStringLiteral("—"));
        m_insTileOtel->setText(QStringLiteral("—"));
        m_insTileOther->setText(QStringLiteral("0"));
    } else {
        int other = 0;
        bool hasDd = false;
        bool hasOtel = false;
        for (const QString &agent : snapshot.instrumentation) {
            if (agent == QStringLiteral("datadog")) {
                hasDd = true;
            } else if (agent == QStringLiteral("opentelemetry")) {
                hasOtel = true;
            } else {
                ++other;
            }
        }
        m_insTileAgents->setText(
            QString::number(snapshot.instrumentation.size()));
        m_insTileDd->setText(hasDd ? QStringLiteral("yes")
                                   : QStringLiteral("—"));
        m_insTileDd->setStyleSheet(
            QStringLiteral("color: %1;").arg(
                hasDd ? ui_theme::ok() : ui_theme::text()));
        m_insTileOtel->setText(hasOtel ? QStringLiteral("yes")
                                       : QStringLiteral("—"));
        m_insTileOtel->setStyleSheet(
            QStringLiteral("color: %1;").arg(
                hasOtel ? ui_theme::ok() : ui_theme::text()));
        m_insTileOther->setText(QString::number(other));
    }

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
        if (snapshot.cgroupHasMemPeak) {
            text += QStringLiteral("\nmemory peak %1")
                        .arg(formatMemoryKb(snapshot.cgroupMemPeakBytes / 1024));
            if (snapshot.cgroupMemLimit > 0) {
                const double row = 100.0
                    * static_cast<double>(snapshot.cgroupMemPeakBytes)
                    / static_cast<double>(snapshot.cgroupMemLimit);
                text += QStringLiteral("  (%1% of max)").arg(row, 0, 'f', 0);
            }
        }
        text += QStringLiteral("\noom events %1  kills %2")
                    .arg(snapshot.cgroupOomEvents)
                    .arg(snapshot.cgroupOomKills);
        if (snapshot.cgroupPidsCurrent >= 0) {
            text += QStringLiteral("\npids  %1 / %2")
                        .arg(snapshot.cgroupPidsCurrent)
                        .arg(snapshot.cgroupPidsMax < 0
                                 ? QStringLiteral("max (unlimited)")
                                 : QString::number(snapshot.cgroupPidsMax));
        }
        m_cgroup->setText(text);
        m_dockTileCgMem->setText(
            formatMemoryKb(snapshot.cgroupMemUsage / 1024));
    } else if (!m_osCgroups) {
        m_cgroup->setText(unsupportedOsText());
        m_dockTileCgMem->setText(QStringLiteral("n/a"));
    } else {
        m_cgroup->setText(QStringLiteral("no cgroup stats"));
        m_dockTileCgMem->setText(QStringLiteral("--"));
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
            if (m_hostCpuSpark != nullptr) {
                m_hostCpuSpark->setValues(m_hostCpuStore.values());
            }
        }
        if (snapshot.kernelLimits.available
            && snapshot.hostStealPercent >= 0.0 && !snapshot.hostWarming) {
            m_stealStore.push(snapshot.hostStealPercent);
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
    if (m_stealStore.size() > 0) {
        watchdog.append(WatchdogMetric{
            QStringLiteral("steal"),
            detectAnomaly(m_stealStore.values(), AnomalyModel::Mad, 3.5)});
    }
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

    // Profiles-page tiles (M61y): thread counts, cpu and pss totals;
    // gated OSes keep profile.available false -> honest dashes.
    if (snapshot.profile.available && !snapshot.profile.warming) {
        m_profTileThreads->setText(
            QString::number(snapshot.profile.threadCount));
        m_profTileCpu->setText(
            QStringLiteral("%1").arg(snapshot.profile.totalCpuPercent,
                                     0, 'f', 1));
        m_profTileOffCpu->setText(
            QString::number(snapshot.profile.waitChannels.size()));
        m_profTilePss->setText(QStringLiteral("%1").arg(
            snapshot.profile.totalPssBytes / 1048576.0, 0, 'f', 1));
        if (m_chartProfile != nullptr) {
            m_chartProfile->push(snapshot.profile.totalCpuPercent);
        }
    } else {
        m_profTileThreads->setText(QStringLiteral("--"));
        m_profTileCpu->setText(QStringLiteral("--"));
        m_profTileOffCpu->setText(QStringLiteral("--"));
        m_profTilePss->setText(QStringLiteral("--"));
    }

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

    {
        const linuxproc::KernelLimits &limits = snapshot.kernelLimits;
        if (limits.available) {
            QString kernelText =
                QStringLiteral("fd %1 / %2%3\n")
                    .arg(limits.fdAllocated)
                    .arg(limits.fdMax)
                    .arg(limits.fdPressure ? QStringLiteral("  !! FD pressure")
                                            : QString());
            kernelText +=
                QStringLiteral("somaxconn %1  cc %2\n")
                    .arg(limits.somaxconn)
                    .arg(limits.congestion.isEmpty()
                             ? QStringLiteral("--")
                             : limits.congestion);
            kernelText +=
                QStringLiteral("inotify %1  pid_max %2\n")
                    .arg(limits.inotifyWatchesMax)
                    .arg(limits.pidMax);
            kernelText +=
                QStringLiteral("swappiness %1  overcommit %2\n")
                    .arg(limits.swappiness)
                    .arg(limits.overcommit);
            kernelText +=
                QStringLiteral("run %1  blocked %2  steal %3%")
                    .arg(snapshot.hostRun < 0
                             ? QStringLiteral("--")
                             : QString::number(snapshot.hostRun))
                    .arg(snapshot.hostBlocked < 0
                             ? QStringLiteral("--")
                             : QString::number(snapshot.hostBlocked))
                    .arg(snapshot.hostStealPercent < 0.05
                             ? QStringLiteral("0.0")
                             : QString::number(
                                   snapshot.hostStealPercent, 'f', 1));
            m_kernel->setText(kernelText);
        } else {
            m_kernel->setText(unsupportedOsText());
        }
    }

    if (snapshot.security.available) {
        m_securitySummary->setText(securitySummaryText(snapshot.security));
        m_security->setText(securityDetailText(snapshot.security));
        // Security-page tiles (M61w): exposure + finding counts.
        m_secTileExposure->setText(
            QStringLiteral("%1").arg(snapshot.security.exposure, 0, 'f', 1));
        m_secTileExposure->setStyleSheet(
            QStringLiteral("color: %1;").arg(
                snapshot.security.exposure >= 7.5 ? ui_theme::crit()
                : snapshot.security.exposure >= 2.5 ? ui_theme::warn()
                                                    : ui_theme::text()));
        m_secTileFindings->setText(
            QString::number(snapshot.security.exposedCount));
    } else if (!m_osSecurity) {
        m_securitySummary->setText(unsupportedOsText());
        m_security->setText(unsupportedOsText());
        m_secTileExposure->setText(QStringLiteral("n/a"));
        m_secTileFindings->setText(QStringLiteral("n/a"));
    } else {
        m_securitySummary->setText(QStringLiteral("--"));
        m_security->setText(QStringLiteral("--"));
        m_secTileExposure->setText(QStringLiteral("--"));
        m_secTileFindings->setText(QStringLiteral("--"));
    }

    m_softwareSummary->setText(sbomSummaryText(snapshot.sbom));
    // Software-page tiles (M61q): package counts + purl families.
    if (snapshot.sbom.available) {
        m_sweTilePkgs->setText(QString::number(
            snapshot.sbom.entries.size()));
        m_sweTileLibs->setText(QString::number(snapshot.sbom.libraryCount));
        m_sweTileResolved->setText(
            QString::number(snapshot.sbom.resolvedCount));
        QSet<QString> fams;
        for (const SbomEntry &entry : snapshot.sbom.entries) {
            const int pkgTag = entry.purl.indexOf(QLatin1Char(':'));
            const int famSlash = entry.purl.indexOf(QLatin1Char('/'),
                                                    pkgTag + 1);
            if (pkgTag >= 0 && famSlash > pkgTag) {
                fams.insert(entry.purl.mid(pkgTag + 1,
                                           famSlash - pkgTag - 1));
            }
        }
        m_sweTileFams->setText(QString::number(fams.size()));
    } else {
        m_sweTilePkgs->setText(QStringLiteral("--"));
        m_sweTileLibs->setText(QStringLiteral("--"));
        m_sweTileResolved->setText(QStringLiteral("--"));
        m_sweTileFams->setText(QStringLiteral("--"));
    }
    m_software->setText(snapshot.sbom.available
                            ? sbomDetailText(snapshot.sbom)
                            : (m_osDpkgSbom ? QStringLiteral("--")
                                            : unsupportedOsText()));

    updateSloPanel(snapshot.slo);

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

    // Dependencies-page tiles (M61r): SBOM resolution quality; all four
    // tiles share the sbom source so the page stays self-consistent.
    if (snapshot.sbom.available) {
        m_depTileLibs->setText(
            QString::number(snapshot.sbom.libraryCount));
        m_depTilePkg->setText(QString::number(snapshot.sbom.entries.size()));
        const int libTotal = qMax(1, snapshot.sbom.libraryCount);
        const int unmapped = qMax(0, snapshot.sbom.libraryCount
                                       - snapshot.sbom.resolvedCount);
        m_depTileUnmapped->setText(QString::number(unmapped));
        m_depTileRatio->setText(
            QStringLiteral("%1%").arg(
                100.0 * snapshot.sbom.resolvedCount / libTotal, 0, 'f', 0));
    } else {
        m_depTileLibs->setText(QStringLiteral("--"));
        m_depTilePkg->setText(QStringLiteral("--"));
        m_depTileUnmapped->setText(QStringLiteral("--"));
        m_depTileRatio->setText(QStringLiteral("--"));
    }

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
    // Container-page tiles (M63): running/issues/total; issues colored
    // red while nonzero, back to the palette text when clear.
    m_dockTileRun->setText(QString::number(snapshot.runningCount));
    m_dockTileIssues->setText(QString::number(snapshot.issueCount));
    m_dockTileIssues->setStyleSheet(
        snapshot.issueCount > 0
            ? QStringLiteral("color: %1;").arg(ui_theme::crit())
            : QStringLiteral("color: %1;").arg(ui_theme::text()));
    m_dockTileTotal->setText(QString::number(snapshot.containers.size()));

    m_dockerTable->clear();
    for (const DockerContainer &container : snapshot.containers) {
        auto *item = new QTreeWidgetItem(m_dockerTable);
        item->setText(0, container.name.isEmpty() ? container.id
                                                  : container.name);
        item->setText(1, container.state);
        item->setText(2, container.status);
        const bool bad = container.crashed
            || container.health == QLatin1String("unhealthy")
            || (container.state != QLatin1String("running")
                && container.state != QLatin1String("healthy"));
        item->setForeground(1, QBrush(QColor(
            bad ? ui_theme::crit() : ui_theme::ok())));
    }

    const QString summary = dockerSummaryText(snapshot);
    const QString details = dockerDetailText(snapshot);
    QString text = summary;
    if (!details.isEmpty() && details != QStringLiteral("no issues")) {
        text += QStringLiteral("\n") + details;
    }
    m_docker->setText(text.isEmpty() ? QStringLiteral("--") : text);

    if (m_chartDocker != nullptr
        && (snapshot.state == DockerState::Ok
            || snapshot.state == DockerState::Stale)) {
        m_chartDocker->push(static_cast<double>(snapshot.runningCount));
    }
}

void DashboardPanel::setKubeSnapshot(const KubeSnapshot &snapshot) {
    const QString summary = kubeSummaryText(snapshot);
    m_kubeSummary->setText(summary.isEmpty() ? QStringLiteral("--") : summary);

    // Cluster-page tiles (M64); issues colored red while nonzero.
    m_kubeTilePods->setText(QString::number(snapshot.totalPods));
    m_kubeTileIssues->setText(QString::number(snapshot.issueCount));
    m_kubeTileIssues->setStyleSheet(
        snapshot.issueCount > 0
            ? QStringLiteral("color: %1;").arg(ui_theme::crit())
            : QStringLiteral("color: %1;").arg(ui_theme::text()));
    m_kubeTileDeploys->setText(QString::number(snapshot.deployments.size()));
    if (snapshot.costPods > 0) {
        m_kubeTileCost->setText(
            QStringLiteral("$%1").arg(snapshot.costMonthly, 0, 'f', 1));
    } else {
        m_kubeTileCost->setText(QStringLiteral("--"));
    }

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
    if (haveData && m_chartKube != nullptr) {
        m_chartKube->push(static_cast<double>(snapshot.totalPods));
    }
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
    // Services-page tiles (M61n): running/failed/units/changes; failed
    // red while > 0, honest "--" until the SCM/systemd sample lands.
    if (snapshot.available) {
        m_svcTileRunning->setText(QString::number(snapshot.runningCount));
        m_svcTileFailed->setText(QString::number(snapshot.failedCount));
        m_svcTileFailed->setStyleSheet(
            snapshot.failedCount > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        m_svcTileUnits->setText(QString::number(snapshot.units.size()));
        m_svcTileChanges->setText(QString::number(snapshot.changes.size()));
    } else {
        m_svcTileRunning->setText(QStringLiteral("--"));
        m_svcTileFailed->setText(QStringLiteral("--"));
        m_svcTileUnits->setText(QStringLiteral("--"));
        m_svcTileChanges->setText(QStringLiteral("--"));
    }

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
    // Logs-page tiles + volume history (M61o); honest "--" until the
    // journal sample lands, "n/a" on OSes without a journal.
    if (snapshot.available) {
        const int lines = snapshot.entries.size();
        m_logTileEntries->setText(QString::number(lines));
        m_logTileErrors->setText(QString::number(snapshot.errorCount));
        m_logTileErrors->setStyleSheet(
            snapshot.errorCount > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        if (m_chartLog != nullptr) {
            m_chartLog->push(static_cast<double>(lines));
        }
    } else {
        m_logTileEntries->setText(m_osJournal ? QStringLiteral("--")
                                              : QStringLiteral("n/a"));
        m_logTileErrors->setText(m_osJournal ? QStringLiteral("--")
                                             : QStringLiteral("n/a"));
    }

    m_logsSummary->setText(journalSummaryText(snapshot));
    m_logs->setText(snapshot.available
                        ? journalLogsText(snapshot, 40)
                        : (m_osJournal ? QStringLiteral("--")
                                       : unsupportedOsText()));
    m_errors->setText(snapshot.available
                          ? journalErrorGroupsText(snapshot.errorGroups)
                          : (m_osJournal ? QStringLiteral("--")
                                         : unsupportedOsText()));
    // Errors-page tiles (M61p): groups/total/top-group/worst severity.
    if (snapshot.available) {
        int topGroup = 0;
        for (const ErrorGroup &group : snapshot.errorGroups) {
            topGroup = qMax(topGroup, group.count);
        }
        int worstPriority = 7;
        for (const JournalEntry &entry : snapshot.entries) {
            worstPriority = qMin(worstPriority, entry.priority);
        }
        const bool hasEntries = !snapshot.entries.isEmpty();
        m_errTileGroups->setText(QString::number(snapshot.errorGroups.size()));
        m_errTileCount->setText(QString::number(snapshot.errorCount));
        m_errTileCount->setStyleSheet(
            snapshot.errorCount > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        m_errTileWorst->setText(QString::number(topGroup));
        m_errTileWorst->setStyleSheet(
            QStringLiteral("color: %1;").arg(ui_theme::text()));
        m_errTileRate->setText(hasEntries
                                   ? QString::number(worstPriority)
                                   : QStringLiteral("--"));
        m_errTileRate->setStyleSheet(
            hasEntries
                ? QStringLiteral("color: %1;").arg(
                    worstPriority <= 3 ? ui_theme::crit()
                                       : ui_theme::text())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
    } else {
        m_errTileGroups->setText(m_osJournal ? QStringLiteral("--")
                                             : QStringLiteral("n/a"));
        m_errTileCount->setText(m_osJournal ? QStringLiteral("--")
                                            : QStringLiteral("n/a"));
        m_errTileWorst->setText(QStringLiteral("--"));
        m_errTileRate->setText(QStringLiteral("--"));
    }

    QStringList messages;
    for (const JournalEntry &entry : snapshot.entries) {
        messages.append(entry.message);
    }
    const LogFacets facets = computeLogFacets(messages);
    m_logTileLevels->setText(
        snapshot.available || !facets.levels.isEmpty()
            ? QString::number(facets.levels.size())
            : QStringLiteral("--"));
    m_logTilePipeline->setText(
        facets.total > 0
            ? QStringLiteral("%1%").arg(
                100.0 * facets.parsed / facets.total, 0, 'f', 0)
            : QStringLiteral("--"));
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
    // Security-page THREATS tile (M61w); red while overflow scenarios
    // are firing.
    m_secTileThreats->setText(QString::number(threats.findings.size()));
    m_secTileThreats->setStyleSheet(
        threats.findings.isEmpty()
            ? QStringLiteral("color: %1;").arg(ui_theme::text())
            : QStringLiteral("color: %1;").arg(ui_theme::crit()));
}

void DashboardPanel::setAlerts(const QList<Alert> &alerts) {
    // Monitor-status tile counts (gridded per DD status vocabulary:
    // status:critical / status:warning / status:ok live in the agent).
    int crit = 0;
    int warn = 0;
    QSet<QString> nowSeen;
    for (const Alert &alert : alerts) {
        const QString key = alert.id.isEmpty()
            ? QStringLiteral("%1/%2").arg(alert.title, alert.severity)
            : alert.id;
        nowSeen.insert(key);
        if (alert.severity.compare(QLatin1String("critical"),
                                   Qt::CaseInsensitive) == 0
            || alert.severity.compare(QLatin1String("high"),
                                      Qt::CaseInsensitive) == 0) {
            ++crit;
        } else {
            ++warn;
        }
    }
    m_alOpen->setText(QString::number(alerts.size()));
    m_alCrit->setText(QString::number(crit));
    m_alWarn->setText(QString::number(warn));
    m_alCrit->setStyleSheet(
        crit > 0 ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                 : QStringLiteral("color: %1;").arg(ui_theme::text()));
    m_alWarn->setStyleSheet(
        warn > 0 ? QStringLiteral("color: %1;").arg(ui_theme::warn())
                 : QStringLiteral("color: %1;").arg(ui_theme::text()));

    // Seen-id diff -> transitions. Appeared alerts count as fired; ids
    // that dropped from the active list count as resolved (AlertEngine
    // maintains active; each call here is one cadence tick).
    const QSet<QString> appeared = nowSeen - m_alertSeen;
    const QSet<QString> vanished = m_alertSeen - nowSeen;
    m_alertSeen = nowSeen;
    const int fired = appeared.size();
    const int resolved = vanished.size();
    m_alertsFired += static_cast<quint64>(fired);
    m_alertsResolved += static_cast<quint64>(resolved);
    m_alResolved->setText(QString::number(m_alertsResolved));

    if (m_chartAlertTrans != nullptr) {
        m_chartAlertTrans->push(static_cast<double>(fired));
        m_chartAlertTrans->pushB(static_cast<double>(resolved));
    }

    // Event timeline: newest first, colored severity, bounded rows.
    QString timeline = m_alertTimeline->text();
    if (timeline == QStringLiteral("--")) {
        timeline.clear();
    }
    const QString stamp =
        QDateTime::fromMSecsSinceEpoch(
            QDateTime::currentMSecsSinceEpoch()).time().toString(
            QStringLiteral("HH:mm"));
    if (!timeline.isEmpty()) {
        timeline.prepend(QStringLiteral("<br/>"));
    }
    if (fired > 0 || resolved > 0) {
        QSet<QString> firedKeys = appeared;
        for (const Alert &alert : alerts) {
            const QString key = alert.id.isEmpty()
                ? QStringLiteral("%1/%2").arg(alert.title, alert.severity)
                : alert.id;
            if (!firedKeys.contains(key) || alert.severity.isEmpty()) {
                continue;
            }
            firedKeys.remove(key);
            const QString sev = alert.severity.toUpper();
            timeline.prepend(
                QStringLiteral(
                    "%1&nbsp;&nbsp;<span style=\"color:%2;\">fired</span>"
                    "&nbsp;&nbsp;<span style=\"color:%3;\">[%4]</span>"
                    "&nbsp;%5&nbsp;%6")
                    .arg(stamp,
                         QStringLiteral("#3ccaaa"),
                         ui_theme::statusColor(sev),
                         sev,
                         alert.title,
                         alert.detail));
            timeline.prepend(QStringLiteral("<br/>"));
        }
        for (const QString &key : vanished) {
            timeline.prepend(
                QStringLiteral(
                    "%1&nbsp;&nbsp;<span style=\"color:%2;\">resolved</span> "
                    "&nbsp;%3")
                    .arg(stamp,
                         QStringLiteral("#4d9fd6"),
                         key));
            timeline.prepend(QStringLiteral("<br/>"));
        }
    }
    const QStringList rows = timeline.split(QStringLiteral("<br/>"),
                                            Qt::SkipEmptyParts);
    m_alertTimeline->setText(
        rows.isEmpty()
            ? QStringLiteral("no alert activity yet")
            : rows.mid(0, 14).join(QStringLiteral("<br/>")));

    // Active monitor list (behavior + rich-text spans unchanged; the
    // test pins #d1685f severity coloring here). The empty state needs
    // an inline dim color: the #ddAlerts QSS rule was removed in the
    // M60 restyle so severity spans stay colorized.
    if (alerts.isEmpty()) {
        m_alerts->setStyleSheet(
            QStringLiteral("color: %1;").arg(ui_theme::dim()));
        m_alerts->setText(QStringLiteral("no active alerts"));
        return;
    }
    m_alerts->setStyleSheet(QStringLiteral(""));
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

void DashboardPanel::updateSloPanel(const SloReport &report) {
    const bool live = report.available && report.total > 0;
    if (!live) {
        m_sloSli->setText(QStringLiteral("--"));
        m_sloObj->setText(QStringLiteral("--"));
        m_sloBudget->setText(QStringLiteral("--"));
        m_sloBurn->setText(QStringLiteral("--"));
        m_sloBurn->setStyleSheet(QStringLiteral("color: %1;")
                                     .arg(ui_theme::text()));
        m_sloSummary->setStyleSheet(
            QStringLiteral("color: %1;").arg(ui_theme::dim()));
        m_sloSummary->setText(QStringLiteral("collecting availability samples"));
        m_slo->setStyleSheet(QStringLiteral("color: %1;")
                                 .arg(ui_theme::dim()));
        m_slo->setText(QStringLiteral("no burn windows yet"));
        return;
    }
    const QString sliPct = QString::number(report.sli * 100.0, 'f', 3);
    const QString objPct = QString::number(report.objective * 100.0, 'f', 3);
    const double budgetPct = qBound<double>(
        0.0, report.budgetRemaining * 100.0, 100.0);
    const QString budPct = QString::number(budgetPct, 'f', 1);
    m_sloSli->setText(sliPct + QStringLiteral("%"));
    m_sloObj->setText(objPct + QStringLiteral("%"));
    m_sloBudget->setText(budPct + QStringLiteral("%"));
    m_sloBudget->setStyleSheet(
        budgetPct <= 0.0
            ? QStringLiteral("color: %1;").arg(ui_theme::crit())
            : QStringLiteral("color: %1;").arg(ui_theme::text()));

    double burn = 0.0;
    for (const SloWindow &window : report.windows) {
        burn = qMax(burn, window.burnRate);
    }
    const QString burnText = burn >= 10.0
        ? QString::number(burn, 'f', 0)
        : QString::number(burn, 'f', 2);
    m_sloBurn->setText(burnText + QStringLiteral("x"));
    m_sloBurn->setStyleSheet(QStringLiteral("color: %1;").arg(
        burn >= 1.0 ? ui_theme::crit()
                    : (burn >= 0.25 ? ui_theme::warn()
                                    : ui_theme::ok())));

    // Attainment card (Nobl9 model vocabulary: objective / SLI / budget;
    // chip state = budget breached or within).
    m_sloSummary->setStyleSheet(QStringLiteral(""));
    m_sloSummary->setText(
        QStringLiteral(
            "%1<br/><span style=\"color:%2;\">%3</span>&nbsp;&nbsp;%4 good "
            "/ %5 bad of %6 samples")
            .arg(report.name.toHtmlEscaped(),
                 report.breached ? ui_theme::crit() : ui_theme::ok(),
                 report.breached ? QStringLiteral("BUDGET EXHAUSTED")
                                 : QStringLiteral("target met"))
            .arg(report.good)
            .arg(report.bad)
            .arg(report.total));

    // Burn-rate rows: one per rolling window, colored by consumption speed.
    QString burnRows;
    for (const SloWindow &window : report.windows) {
        const QString rateColor = window.burnRate >= 1.0
            ? ui_theme::crit()
            : (window.burnRate >= 0.25 ? ui_theme::warn()
                                       : ui_theme::ok());
        burnRows += QStringLiteral(
            "%1&nbsp;&nbsp;&nbsp;<span style=\"color:%2;\">%3x</span>")
                        .arg(window.label,
                             rateColor,
                             QString::number(window.burnRate, 'f', 2))
                    + QStringLiteral("<br/>");
    }
    m_slo->setStyleSheet(QStringLiteral(""));
    m_slo->setText(burnRows.trimmed());

    if (m_chartSlo != nullptr) {
        m_chartSlo->push(qBound<double>(0.0, report.sli * 100.0, 100.0));
        m_chartSlo->pushB(qBound<double>(0.0,
                                         report.budgetRemaining * 100.0,
                                         100.0));
    }
}

void DashboardPanel::setIncidents(const QList<Incident> &incidents) {
    // Tile counts off the grouped tracker snapshot.
    int open = 0;
    int crit = 0;
    int warn = 0;
    int resolved = 0;
    for (const Incident &incident : incidents) {
        if (incident.status != QStringLiteral("open")) {
            ++resolved;
            continue;
        }
        ++open;
        if (incident.severity.compare(QLatin1String("critical"),
                                      Qt::CaseInsensitive) == 0
            || incident.severity.compare(QLatin1String("high"),
                                         Qt::CaseInsensitive) == 0) {
            ++crit;
        } else {
            ++warn;
        }
    }
    m_incOpen->setText(QString::number(open));
    m_incCrit->setText(QString::number(crit));
    m_incWarn->setText(QString::number(warn));
    m_incResolved->setText(QString::number(resolved));
    m_incCrit->setStyleSheet(
        crit > 0 ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                 : QStringLiteral("color: %1;").arg(ui_theme::text()));
    m_incWarn->setStyleSheet(
        warn > 0 ? QStringLiteral("color: %1;").arg(ui_theme::warn())
                 : QStringLiteral("color: %1;").arg(ui_theme::text()));

    if (m_chartIncOpen != nullptr) {
        m_chartIncOpen->push(static_cast<double>(open));
    }

    // Grouped active list (Alertmanager-style fold-by-key rows).
    if (open == 0) {
        m_incidentsSummary->setStyleSheet(
            QStringLiteral("color: %1;").arg(ui_theme::dim()));
        m_incidentsSummary->setText(QStringLiteral("no open incidents"));
        m_incidents->setText(incidentsDetailText(incidents));
        return;
    }
    m_incidentsSummary->setStyleSheet(QStringLiteral(""));
    QString text;
    int rows = 0;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (const Incident &incident : incidents) {
        if (incident.status != QStringLiteral("open") || rows++ >= 14) {
            continue;
        }
        const QString sev = incident.severity.toUpper();
        const qint64 ageMs = incident.openedAt > 0
            ? qMax<qint64>(0, nowMs - incident.openedAt)
            : -1;
        const QString age = ageMs < 0
            ? QString()
            : (ageMs >= 3600000
                ? QStringLiteral("%1h").arg(ageMs / 3600000)
                : QStringLiteral("%1m").arg(ageMs / 60000));
        text += QStringLiteral(
            "<span style=\"color:%1;\">[OPEN]</span>"
            "&nbsp;&nbsp;<span style=\"color:%2;\">[%3]</span>"
            "&nbsp;%4&nbsp;&nbsp;x%5")
            .arg(ui_theme::accent(),
                 ui_theme::statusColor(sev),
                 sev,
                 incident.title)
            .arg(incident.count)
            + (age.isEmpty()
                   ? QString()
                   : QStringLiteral("&nbsp;&middot;&nbsp;&nbsp;%1").arg(age))
            + QStringLiteral("<br/>");
    }
    m_incidentsSummary->setText(text.trimmed());
    // Bounded history (grouped rows incl. resolved snapshots) keeps the
    // Alertmanager-grid formatting from M35.
    m_incidents->setText(incidentsDetailText(incidents));
}

void DashboardPanel::setSyntheticSnapshot(const SyntheticSnapshot &snapshot) {
    m_syntheticSummary->setText(syntheticSummaryText(snapshot));
    m_synthetic->setText(syntheticDetailText(snapshot));

    // Synthetic-page tiles + latency history (M61t); the agent ships
    // no synthetic metric (SaaS-scheduled tests), so the rate series
    // name is Syspeek-own.
    if (snapshot.available && !snapshot.results.isEmpty()) {
        double totalMs = 0.0;
        int measured = 0;
        for (const ProbeResult &probe : snapshot.results) {
            if (probe.success && probe.totalMs > 0.0) {
                totalMs += probe.totalMs;
                ++measured;
            }
        }
        m_synTileChecks->setText(QString::number(snapshot.results.size()));
        m_synTileUp->setText(QString::number(snapshot.upCount));
        m_synTileDown->setText(QString::number(snapshot.downCount));
        m_synTileDown->setStyleSheet(
            snapshot.downCount > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        m_synTileAvg->setText(
            measured > 0
                ? QStringLiteral("%1").arg(totalMs / measured, 0, 'f', 0)
                : QStringLiteral("--"));
        if (m_chartSynthetic != nullptr && measured > 0) {
            m_chartSynthetic->push(totalMs / measured);
        }
    } else {
        m_synTileChecks->setText(QStringLiteral("--"));
        m_synTileUp->setText(QStringLiteral("--"));
        m_synTileDown->setText(QStringLiteral("--"));
        m_synTileAvg->setText(QStringLiteral("--"));
    }
}

void DashboardPanel::setTraceSnapshot(const TraceSnapshot &snapshot) {
    m_traceSummary->setText(traceSummaryText(snapshot));
    m_traceMap->setText(traceServiceMapText(snapshot));
    m_traceServices->setText(traceServicesText(snapshot));

    // Traces-page tiles (M61v): span flow + mean service latency.
    if (snapshot.available) {
        m_traceTileTraces->setText(QString::number(snapshot.traceCount));
        m_traceTileSpans->setText(QString::number(snapshot.spanCount));
        m_traceTileErrors->setText(QString::number(snapshot.errorCount));
        m_traceTileErrors->setStyleSheet(
            snapshot.errorCount > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        m_traceTileServices->setText(
            QString::number(snapshot.services.size()));
        double sum = 0.0;
        for (const ServiceLatency &svc : snapshot.services) {
            sum += svc.avgMs;
        }
        if (m_chartTrace != nullptr && !snapshot.services.isEmpty()) {
            m_chartTrace->push(sum / snapshot.services.size());
        }
    } else {
        m_traceTileTraces->setText(QStringLiteral("--"));
        m_traceTileSpans->setText(QStringLiteral("--"));
        m_traceTileErrors->setText(QStringLiteral("--"));
        m_traceTileServices->setText(QStringLiteral("--"));
    }
    m_traceRecent->setText(traceRecentText(snapshot));
}

void DashboardPanel::setPathSnapshot(const PathSnapshot &snapshot) {
    // Path-page tiles (M61m): honest "--" until mtr reports arrive.
    if (!snapshot.available || snapshot.reports.isEmpty()) {
        m_pathTileTargets->setText(QStringLiteral("--"));
        m_pathTileHops->setText(QStringLiteral("--"));
        m_pathTileLoss->setText(QString());
        m_pathTileLoss->setStyleSheet(QStringLiteral("color: %1;")
                                          .arg(ui_theme::text()));
        m_pathTileLoss->setText(QStringLiteral("--"));
        m_pathTileRtt->setText(QStringLiteral("--"));
    } else {
        int hopCount = 0;
        double worstLoss = 0.0;
        for (const PathReport &report : snapshot.reports) {
            hopCount = qMax(hopCount, report.hops.size());
            worstLoss = qMax(worstLoss, report.worstLoss());
        }
        const double rtt = snapshot.reports.first().avgRttMs();
        m_pathTileTargets->setText(
            QString::number(snapshot.reports.size()));
        m_pathTileHops->setText(QString::number(hopCount));
        m_pathTileLoss->setText(
            QStringLiteral("%1%").arg(worstLoss, 0, 'f', 1));
        m_pathTileLoss->setStyleSheet(
            QStringLiteral("color: %1;").arg(
                worstLoss >= 1.0 ? ui_theme::crit()
                                 : ui_theme::text()));
        m_pathTileRtt->setText(
            QStringLiteral("%1ms").arg(rtt, 0, 'f', 1));
        if (m_chartPath != nullptr) {
            m_chartPath->push(qMax(0.0, rtt));
        }
    }

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

    m_statMetrics->setText(QString::number(snapshot.metrics));
    m_statEvents->setText(QString::number(snapshot.events));
    m_statSvc->setText(QString::number(snapshot.serviceChecks));
    m_statErrors->setText(QString::number(snapshot.parseErrors));
    m_statErrors->setStyleSheet(
        snapshot.parseErrors > 0
            ? QStringLiteral("color: %1;").arg(ui_theme::crit())
            : QStringLiteral("color: %1;").arg(ui_theme::text()));

    // Per-tick ingest rates from cumulative counters (reset-aware).
    const quint64 dMetrics =
        snapshot.metrics >= m_lastStatMetrics
            ? snapshot.metrics - m_lastStatMetrics
            : snapshot.metrics;
    const quint64 dEvents =
        snapshot.events >= m_lastStatEvents
            ? snapshot.events - m_lastStatEvents
            : snapshot.events;
    const quint64 dSvc =
        snapshot.serviceChecks >= m_lastStatSvc
            ? snapshot.serviceChecks - m_lastStatSvc
            : snapshot.serviceChecks;
    const quint64 dErr =
        snapshot.parseErrors >= m_lastStatErr
            ? snapshot.parseErrors - m_lastStatErr
            : snapshot.parseErrors;
    const bool counts =
        snapshot.datagrams > 0 && !m_firstStatsdTick;
    m_lastStatMetrics = snapshot.metrics;
    m_lastStatEvents = snapshot.events;
    m_lastStatSvc = snapshot.serviceChecks;
    m_lastStatErr = snapshot.parseErrors;
    m_firstStatsdTick = false;

    if (counts) {
        m_statMetricsStore.push(static_cast<double>(dMetrics));
        m_statEventsStore.push(static_cast<double>(dEvents));
        m_statSvcStore.push(static_cast<double>(dSvc));
        m_statErrStore.push(static_cast<double>(dErr));
        if (m_chartStatRate != nullptr) {
            m_chartStatRate->push(static_cast<double>(dMetrics));
        }
        if (m_chartStatEvents != nullptr) {
            m_chartStatEvents->push(static_cast<double>(dEvents));
            m_chartStatEvents->pushB(static_cast<double>(dSvc));
        }
        if (m_chartStatErrors != nullptr) {
            m_chartStatErrors->push(static_cast<double>(dErr));
        }
    }
    setDeltaLabel(m_deltaMetrics, m_statMetricsStore, 2.0, true);
    setDeltaLabel(m_deltaEvents, m_statEventsStore, 2.0, true);
    setDeltaLabel(m_deltaSvc, m_statSvcStore, 2.0, true);
    setDeltaLabel(m_deltaErr, m_statErrStore, 2.0, true);

    // Series explorer: top rows by name (snapshot already sorted).
    if (m_statsdSeriesTree != nullptr) {
        m_statsdSeriesTree->clear();
        int shown = 0;
        for (const StatsdSeries &series : snapshot.series) {
            if (shown++ >= 80) {
                auto *more = new QTreeWidgetItem(m_statsdSeriesTree);
                more->setText(0, QStringLiteral("... %1 more")
                                     .arg(snapshot.series.size() - 80));
                more->setForeground(0, QColor(ui_theme::muted()));
                break;
            }
            auto *item = new QTreeWidgetItem(m_statsdSeriesTree);
            item->setText(0, series.name);
            item->setText(1, series.type);
            item->setText(2, series.tags);
            QString value;
            if (series.type == QStringLiteral("counter")) {
                value = QStringLiteral("sum %1")
                            .arg(QString::number(series.sum, 'f', 2));
            } else if (series.type == QStringLiteral("set")) {
                value = QStringLiteral("unique %1").arg(series.unique);
            } else if (series.type == QStringLiteral("gauge")) {
                value = QStringLiteral("last %1")
                            .arg(QString::number(series.last, 'f', 2));
            } else {
                value = QStringLiteral("avg %1")
                            .arg(QString::number(series.avg, 'f', 2));
            }
            item->setText(3, value);
            item->setText(4, QString::number(series.count));
        }
    }
    m_lastStatsd = snapshot;
}

void DashboardPanel::setDbmSnapshot(const DbmSnapshot &snapshot) {
    m_dbmSummary->setText(dbmSummaryText(snapshot));
    m_dbm->setText(dbmDetailText(snapshot));
    m_dbmStatements->setText(dbmStatementsText(snapshot));

    // Databases-page tiles (M61x): the shipped postgres integration's
    // vocabulary (postgresql.buffer_hit/commits/rollbacks/deadlocks).
    if (snapshot.available) {
        m_dbTileCache->setText(
            QStringLiteral("%1%").arg(snapshot.cacheHitPercent, 0, 'f', 1));
        m_dbTileCommits->setText(
            QStringLiteral("%1").arg(snapshot.commitsPerSec, 0, 'f', 1));
        m_dbTileRollbacks->setText(
            QString::number(snapshot.xactRollback));
        m_dbTileRollbacks->setStyleSheet(
            snapshot.xactRollback > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::warn())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        m_dbTileDeadlocks->setText(
            QString::number(snapshot.deadlocks));
        m_dbTileDeadlocks->setStyleSheet(
            snapshot.deadlocks > 0
                ? QStringLiteral("color: %1;").arg(ui_theme::crit())
                : QStringLiteral("color: %1;").arg(ui_theme::text()));
        if (m_chartDb != nullptr) {
            m_chartDb->push(snapshot.commitsPerSec);
            m_chartDb->pushB(static_cast<double>(snapshot.xactRollback));
        }
    } else {
        m_dbTileCache->setText(QStringLiteral("--"));
        m_dbTileCommits->setText(QStringLiteral("--"));
        m_dbTileRollbacks->setText(QStringLiteral("--"));
        m_dbTileDeadlocks->setText(QStringLiteral("--"));
    }
}

void DashboardPanel::setTlsSnapshot(const TlsSnapshot &snapshot) {
    m_tlsSummary->setText(tlsSummaryText(snapshot));
    m_tls->setText(tlsDetailText(snapshot));
}

void DashboardPanel::setFimSnapshot(const FimSnapshot &snapshot) {
    m_fimSummary->setText(m_osFim ? fimSummaryText(snapshot)
                                  : unsupportedOsText());
    m_fim->setText(m_osFim ? fimDetailText(snapshot) : unsupportedOsText());
    // Security-page FILE CHANGES tile (M61w): drift count or n/a on
    // gated OSes.
    if (m_osFim && snapshot.available && !snapshot.baseline) {
        m_secTileFim->setText(QString::number(snapshot.changes.size()));
    } else {
        m_secTileFim->setText(m_osFim ? QStringLiteral("--")
                                      : QStringLiteral("n/a"));
    }
}
