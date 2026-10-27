#pragma once

#include "DisplaySnapshot.h"
#include "MetricsExport.h"
#include "DockerStatus.h"
#include "JournalStatus.h"
#include "KubeStatus.h"
#include "MetricStore.h"
#include "ServicesStatus.h"
#include "AlertEngine.h"
#include "AlertSilence.h"
#include "MonitorBands.h"
#include "SyntheticStatus.h"
#include "TraceStatus.h"
#include "PathStatus.h"
#include "StatsdStatus.h"
#include "ThreatStatus.h"
#include "DbmStatus.h"
#include "WatchdogStatus.h"
#include "TlsStatus.h"
#include "IncidentStatus.h"
#include "FimStatus.h"

#include <QHash>
#include <QSet>
#include <QWidget>

class QLabel;
class QListWidget;
class QStackedWidget;
class QTreeWidget;
class QSlider;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class Sparkline;
class TimeseriesChart;

// Data for the Settings view - static shell facts the sampler does not
// own (version, paths, thresholds from the loaded config, autostart
// across installers). Filled by main.cpp after startup succeeds.
// Local history ring summary (M74): the panel renders the storage facts and
// the stored window; rewinding to a specific sample is answered by the owner
// (HudWindow) through setHistorySelection, so the panel never touches files.
struct HistoryViewInfo {
    bool enabled = false;
    QString path;
    int count = 0;
    int capacity = 0;
    qint64 bytes = 0;
    qint64 spanMs = 0;
    qint64 intervalMs = 1000;
    qint64 newestMs = 0;
    QList<ExportRow> window;
};

struct SettingsInfo {
    QString version;
    QString configPath;
    QString configError;
    QString logPath;
    QString autostartLine;
    QString verbsLine;
    QString metricsLine;
    QString webhookLine;
    QString exportLine;
    QString historyLine;
    int sampleIntervalMs = 1000;
    double cpuWarnPercent = 70.0;
    double cpuHighPercent = 90.0;
    double rssWarnGb = 1.0;
};

// Expanded console shown inside the HUD overlay: left rail of views, each view
// is its own grid of cards so panels never crowd each other.
class DashboardPanel : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPanel(QWidget *parent = nullptr);

    void setSnapshot(const DisplaySnapshot &snapshot);
    void setDockerSnapshot(const DockerSnapshot &snapshot);
    void setKubeSnapshot(const KubeSnapshot &snapshot);
    void setServicesSnapshot(const ServicesSnapshot &snapshot);
    void setJournalSnapshot(const JournalSnapshot &snapshot);
    void setAlerts(const QList<Alert> &alerts);
    void setIncidents(const QList<Incident> &incidents);
    void setSyntheticSnapshot(const SyntheticSnapshot &snapshot);
    void setTraceSnapshot(const TraceSnapshot &snapshot);
    void setPathSnapshot(const PathSnapshot &snapshot);
    void setStatsdSnapshot(const StatsdSnapshot &snapshot);
    void setDbmSnapshot(const DbmSnapshot &snapshot);
    void setTlsSnapshot(const TlsSnapshot &snapshot);
    void setFimSnapshot(const FimSnapshot &snapshot);

    void setSettingsInfo(const SettingsInfo &info);
    void setHistoryView(const HistoryViewInfo &view);
    void setHistorySelection(const QString &position, const QString &detail);

    void setWebhookLine(const QString &line);

    void setExportLine(const QString &line);

    // M66: editable monitors — populates the threshold/cycles editors and
    // refreshes them without re-emitting editor signals (block during fill).
    void setMonitors(const QList<AlertRule> &rules);

    // M75: alert silences (Alertmanager model). The panel renders the
    // authored windows and lets the user snooze a monitor from the Alerts
    // page; the engine keeps evaluating, notification paths stay quiet.
    void setSilences(const QList<SilenceRule> &rules);

signals:
    void collapseRequested();
    void monitorChanged(const AlertRule &rule);
    void monitorsResetRequested();
    void historyExportRequested(const QString &format);
    void historySeekRequested(int index);
    void silenceRequested(const QString &monitorId, int minutes);
    void silenceExpireRequested(const QString &monitorId);

private:
    void updateOverviewText(const DisplaySnapshot &snapshot);
    void updateSloPanel(const SloReport &report);
    QWidget *buildOverviewView();
    QWidget *buildAlertsView();
    QWidget *buildIncidentsView();
    QWidget *buildProfilesView();
    QWidget *buildProcessesView();
    QWidget *buildMemoryView();
    QWidget *buildNetworkView();
    QWidget *buildContainersView();
    QWidget *buildKubernetesView();
    QWidget *buildServicesView();
    QWidget *buildDependenciesView();
    QWidget *buildLogsView();
    QWidget *buildErrorsView();
    QWidget *buildEndpointsView();
    QWidget *buildSyntheticView();
    QWidget *buildInstrumentationView();
    QWidget *buildDatabasesView();
    QWidget *buildHistoryView();
    QWidget *buildCostView();
    QWidget *buildDisksView();
    QWidget *buildGpuView();
    QWidget *buildHostView();
    QWidget *buildSecurityView();
    QWidget *buildTracesView();
    QWidget *buildSoftwareView();
    QWidget *buildNetworkPathView();
    QWidget *buildMetricsView();
    QWidget *buildSloView();
    QWidget *buildSettingsView();
    void applySettingsInfo();
    void copySettingCommand();

    QString unsupportedOsText() const;

    bool m_osTcpCounters = true;
    bool m_osCgroups = true;
    bool m_osPsi = true;
    bool m_osJournal = true;
    bool m_osDpkgSbom = true;
    bool m_osFim = true;
    bool m_osSecurity = true;
    bool m_osProcessNet = true;
    bool m_osProcessDisk = true;
    bool m_osProcessDiskScan = true;
    bool m_osGpu = true;
    bool m_osGpuClients = true;
    bool m_osGpuProcess = true;
    QString m_osName = QStringLiteral("linux");

    QLabel *m_appValue = nullptr;
    QLabel *m_pidValue = nullptr;
    QLabel *m_alerts = nullptr;
    QLabel *m_incidentsSummary = nullptr;
    QLabel *m_incidents = nullptr;
    QLabel *m_profileSummary = nullptr;
    QLabel *m_profileThreads = nullptr;
    QLabel *m_profileOffCpu = nullptr;
    QLabel *m_profileMemory = nullptr;
    QLabel *m_cpuValue = nullptr;
    QLabel *m_memValue = nullptr;
    QLabel *m_procCount = nullptr;
    QLabel *m_statusValue = nullptr;
    QLabel *m_statusBig = nullptr;
    QLabel *m_hostCpu = nullptr;
    QLabel *m_hostMem = nullptr;
    QLabel *m_hostDisk = nullptr;
    QLabel *m_hostNet = nullptr;
    QLabel *m_hostLoad = nullptr;
    QLabel *m_watchdog = nullptr;
    QLabel *m_forecast = nullptr;
    QLabel *m_insights = nullptr;
    QLabel *m_runtime = nullptr;
    QLabel *m_memDetail = nullptr;
    QTreeWidget *m_procs = nullptr;
    QLabel *m_procNetNote = nullptr;
    QLabel *m_procDiskNote = nullptr;
    QLabel *m_focusDetail = nullptr;
    QLabel *m_openFiles = nullptr;
    QLabel *m_net = nullptr;
    QLabel *m_listen = nullptr;
    QLabel *m_states = nullptr;
    QLabel *m_ifaces = nullptr;
    QLabel *m_tcpSummary = nullptr;
    QLabel *m_tcp = nullptr;
    QLabel *m_cgroup = nullptr;
    QLabel *m_docker = nullptr;
    QLabel *m_kubeSummary = nullptr;
    QTreeWidget *m_kubePods = nullptr;
    QTreeWidget *m_kubeDeploys = nullptr;
    QLabel *m_kubeEvents = nullptr;
    QLabel *m_kubeDiagnosis = nullptr;
    QLabel *m_kubeTimeline = nullptr;
    QLabel *m_deprecations = nullptr;
    QLabel *m_nodes = nullptr;
    QLabel *m_costSummary = nullptr;
    QLabel *m_costWarnings = nullptr;
    QTreeWidget *m_costNamespaces = nullptr;
    QTreeWidget *m_costWorkloads = nullptr;
    QLabel *m_diskSummary = nullptr;
    QLabel *m_diskTopNote = nullptr;
    QLabel *m_diskTop = nullptr;
    QLabel *m_diskPressure = nullptr;
    QTreeWidget *m_disks = nullptr;
    QLabel *m_hostInfoSummary = nullptr;
    QLabel *m_hostPlatform = nullptr;
    QLabel *m_hostCpuInfo = nullptr;
    QLabel *m_hostFilesystems = nullptr;
    QLabel *m_hostNetwork = nullptr;
    QLabel *m_kernel = nullptr;
    QLabel *m_securitySummary = nullptr;
    QLabel *m_security = nullptr;
    QLabel *m_threats = nullptr;
    QLabel *m_fimSummary = nullptr;
    QLabel *m_fim = nullptr;
    QLabel *m_dbmSummary = nullptr;
    QLabel *m_dbm = nullptr;
    QLabel *m_dbmStatements = nullptr;
    QLabel *m_tlsSummary = nullptr;
    QLabel *m_tls = nullptr;
    QLabel *m_traceSummary = nullptr;
    QLabel *m_traceMap = nullptr;
    QLabel *m_traceServices = nullptr;
    QLabel *m_traceRecent = nullptr;
    QLabel *m_softwareSummary = nullptr;
    QLabel *m_software = nullptr;
    QLabel *m_pathSummary = nullptr;
    QLabel *m_pathTargets = nullptr;
    QTreeWidget *m_pathHops = nullptr;
    QLabel *m_statsdSummary = nullptr;
    QLabel *m_statsd = nullptr;
    StatsdSnapshot m_lastStatsd;
    QLabel *m_sloSummary = nullptr;
    QLabel *m_slo = nullptr;
    QLabel *m_services = nullptr;
    QLabel *m_servicesSummary = nullptr;
    QLabel *m_servicesChanges = nullptr;
    QLabel *m_deps = nullptr;
    QLabel *m_logs = nullptr;
    QLabel *m_logsSummary = nullptr;
    QLabel *m_logPipeline = nullptr;
    QLabel *m_sensitiveSummary = nullptr;
    QLabel *m_sensitive = nullptr;
    QLabel *m_errors = nullptr;
    QLabel *m_endpoints = nullptr;
    QLabel *m_synthetic = nullptr;
    QLabel *m_syntheticSummary = nullptr;
    QLabel *m_instrumentation = nullptr;
    QLabel *m_databases = nullptr;

    Sparkline *m_cpuSpark = nullptr;
    Sparkline *m_memSpark = nullptr;
    Sparkline *m_hostCpuSpark = nullptr;
    MetricStore m_cpuStore{120};
    MetricStore m_memStore{120};
    MetricStore m_hostCpuStore{120};
    MetricStore m_stealStore{120};

    TimeseriesChart *m_chartCpu = nullptr;
    TimeseriesChart *m_chartMem = nullptr;
    TimeseriesChart *m_chartProc = nullptr;
    TimeseriesChart *m_chartHostCpu = nullptr;
    TimeseriesChart *m_chartHostMem = nullptr;
    TimeseriesChart *m_chartLoad = nullptr;
    TimeseriesChart *m_chartNet = nullptr;
    TimeseriesChart *m_chartDisk = nullptr;
    TimeseriesChart *m_chartDiskUtil = nullptr;
    MetricStore m_procStore{1800};
    MetricStore m_hostMemStore{1800};
    MetricStore m_netRxStore{1800};
    MetricStore m_netTxStore{1800};
    MetricStore m_loadStore{1800};
    MetricStore m_diskReadStore{1800};
    MetricStore m_diskWriteStore{1800};
    MetricStore m_diskUtilStore{1800};
    QLabel *m_deltaCpu = nullptr;
    QLabel *m_deltaMem = nullptr;
    QLabel *m_deltaProc = nullptr;
    QPushButton *m_pause = nullptr;
    QLabel *m_timeChip = nullptr;
    QLabel *m_footer = nullptr;
    bool m_chartsPaused = false;
    qint64 m_lastTickMs = 0;
    double m_cadenceMs = 2000.0;

    // Datadog-style Metrics page (M61b): tiles + telemetry charts +
    // series-explorer table. Counters are cumulative in StatsdSnapshot;
    // the panel tracks per-tick deltas for the rate charts.
    TimeseriesChart *m_chartStatRate = nullptr;
    TimeseriesChart *m_chartStatEvents = nullptr;
    TimeseriesChart *m_chartStatErrors = nullptr;
    MetricStore m_statMetricsStore{1800};
    MetricStore m_statEventsStore{1800};
    MetricStore m_statSvcStore{1800};
    MetricStore m_statErrStore{1800};
    quint64 m_lastStatMetrics = 0;
    quint64 m_lastStatEvents = 0;
    quint64 m_lastStatSvc = 0;
    quint64 m_lastStatErr = 0;
    bool m_firstStatsdTick = true;
    QLabel *m_statMetrics = nullptr;
    QLabel *m_statEvents = nullptr;
    QLabel *m_statSvc = nullptr;
    QLabel *m_statErrors = nullptr;
    QLabel *m_deltaMetrics = nullptr;
    QLabel *m_deltaEvents = nullptr;
    QLabel *m_deltaSvc = nullptr;
    QLabel *m_deltaErr = nullptr;
    QTreeWidget *m_statsdSeriesTree = nullptr;

    // Datadog-style Alerts page (M61c): monitor-status tiles + fired/
    // resolved transition chart + panel-side event timeline. A seen-id
    // diff produces real transitions from the per-tick active list.
    QLabel *m_alOpen = nullptr;
    QLabel *m_alCrit = nullptr;
    QLabel *m_alWarn = nullptr;
    QLabel *m_alResolved = nullptr;
    TimeseriesChart *m_chartAlertTrans = nullptr;
    QLabel *m_alertTimeline = nullptr;
    QList<SilenceRule> m_silences;
    QComboBox *m_silenceTarget = nullptr;
    QSpinBox *m_silenceMinutes = nullptr;
    QPushButton *m_silenceAdd = nullptr;
    QPushButton *m_silenceExpire = nullptr;
    QLabel *m_silenceSummary = nullptr;
    QLabel *m_silenceList = nullptr;
    QCheckBox *m_procHealthyOnly = nullptr;
    QLabel *m_procFilterNote = nullptr;
    QList<ProcessRow> m_lastProcessRows;
    bool m_procFilterActive = false;
    void updateSilencesPanel();
    void requestSilence();
    void expireSelectedSilence();
    void populateProcessTree(const QList<ProcessRow> &rows);
    QSet<QString> m_alertSeen;
    quint64 m_alertsFired = 0;
    quint64 m_alertsResolved = 0;

    // Datadog-style Incidents page (M61d): Alertmanager-grouped tracker
    // snapshots -> status tiles + grouped list + open-over-time chart.
    QLabel *m_incOpen = nullptr;
    QLabel *m_incCrit = nullptr;
    QLabel *m_incWarn = nullptr;
    QLabel *m_incResolved = nullptr;
    TimeseriesChart *m_chartIncOpen = nullptr;

    // Datadog-style SLO page (M61e): attainment tiles + SLI/budget
    // history + per-window burn rates (Nobl9 REA model, M27).
    QLabel *m_sloSli = nullptr;
    QLabel *m_sloObj = nullptr;
    QLabel *m_sloBudget = nullptr;
    QLabel *m_sloBurn = nullptr;
    TimeseriesChart *m_chartSlo = nullptr;

    // Datadog-style Processes page (M62): fleet totals + CPU/load story.
    QLabel *m_procTileProcs = nullptr;
    QLabel *m_procTileThreads = nullptr;
    QLabel *m_procTileFds = nullptr;
    QLabel *m_procTileLoad = nullptr;
    TimeseriesChart *m_chartProcTotals = nullptr;

    // Datadog-style Containers page (M63): docker tiles/tables + cgroup.
    QLabel *m_dockTileRun = nullptr;
    QLabel *m_dockTileIssues = nullptr;
    QLabel *m_dockTileTotal = nullptr;
    QLabel *m_dockTileCgMem = nullptr;
    QTreeWidget *m_dockerTable = nullptr;
    TimeseriesChart *m_chartDocker = nullptr;

    // Datadog-style Kubernetes page (M64): cluster tiles + workloads.
    QLabel *m_kubeTilePods = nullptr;
    QLabel *m_kubeTileIssues = nullptr;
    QLabel *m_kubeTileDeploys = nullptr;
    QLabel *m_kubeTileCost = nullptr;
    TimeseriesChart *m_chartKube = nullptr;

    // Datadog-style Memory page (M61i): host tiles + used/avail history.
    QLabel *m_memTileTotal = nullptr;
    QLabel *m_memTileUsed = nullptr;
    QLabel *m_memTileAvail = nullptr;
    QLabel *m_memTileRss = nullptr;
    TimeseriesChart *m_chartMemPage = nullptr;

    // Datadog-style Disks page (M61j): io tiles + rate/util charts.
    QLabel *m_diskTileRead = nullptr;
    QLabel *m_diskTileWrite = nullptr;
    QLabel *m_diskTileUtil = nullptr;
    QLabel *m_diskTilePsi = nullptr;
    TimeseriesChart *m_chartIoRw = nullptr;
    TimeseriesChart *m_chartIoUtil = nullptr;

    // History page (M74): bounded on-disk ring + rewind scrubber.
    QLabel *m_histTileSamples = nullptr;
    QLabel *m_histTileSpan = nullptr;
    QLabel *m_histTileDisk = nullptr;
    QLabel *m_histTileInterval = nullptr;
    QLabel *m_histSummary = nullptr;
    QLabel *m_histPos = nullptr;
    QLabel *m_histDetail = nullptr;
    QList<QLabel *> m_histDetailLines;
    QSlider *m_histSlider = nullptr;
    QPushButton *m_histFirst = nullptr;
    QPushButton *m_histPrev = nullptr;
    QPushButton *m_histNext = nullptr;
    QPushButton *m_histLast = nullptr;
    TimeseriesChart *m_chartHistCpu = nullptr;
    TimeseriesChart *m_chartHistMem = nullptr;
    HistoryViewInfo m_historyView;
    qint64 m_histWindowNewest = 0;

    // GPU page (M73): device utilization/memory/sensors + gpu processes.
    QLabel *m_gpuTileUtil = nullptr;
    QLabel *m_gpuTileMem = nullptr;
    QLabel *m_gpuTileTemp = nullptr;
    QLabel *m_gpuTileProcs = nullptr;
    QLabel *m_gpuSummary = nullptr;
    QLabel *m_gpuDevices = nullptr;
    QLabel *m_gpuProcessNote = nullptr;
    QTreeWidget *m_gpuTable = nullptr;
    TimeseriesChart *m_chartGpu = nullptr;

    // Datadog-style Host page (M61k): live-vitals tiles + inventory cards.
    QLabel *m_hostTileCpu = nullptr;
    QLabel *m_hostTileLoad = nullptr;
    QLabel *m_hostTileAvail = nullptr;
    QLabel *m_hostTileDisk = nullptr;
    TimeseriesChart *m_chartHostPage = nullptr;

    // Datadog-style Network page (M61l): socket tiles + throughput.
    QLabel *m_netTileConns = nullptr;    QLabel *m_netTileEstab = nullptr;
    QLabel *m_netTileRetrans = nullptr;
    QLabel *m_netTileTx = nullptr;
    TimeseriesChart *m_chartNetPage = nullptr;
    TimeseriesChart *m_chartConnRtt = nullptr;
    QLabel *m_talkers = nullptr;
    QLabel *m_talkerSummary = nullptr;

    // Datadog-style Network Path page (M61m): probe tiles + RTT history.
    QLabel *m_pathTileTargets = nullptr;
    QLabel *m_pathTileHops = nullptr;
    QLabel *m_pathTileLoss = nullptr;
    QLabel *m_pathTileRtt = nullptr;
    TimeseriesChart *m_chartPath = nullptr;

    // Datadog-style Services page (M61n): unit-health tiles + changes.
    QLabel *m_svcTileRunning = nullptr;
    QLabel *m_svcTileFailed = nullptr;
    QLabel *m_svcTileUnits = nullptr;
    QLabel *m_svcTileChanges = nullptr;

    // Datadog-style Logs page (M61o): journal flow tiles + pipeline.
    QLabel *m_logTileEntries = nullptr;
    QLabel *m_logTileErrors = nullptr;
    QLabel *m_logTileLevels = nullptr;
    QLabel *m_logTilePipeline = nullptr;
    TimeseriesChart *m_chartLog = nullptr;

    // Datadog-style Errors page (M61p): grouped errors + volume.
    QLabel *m_errTileGroups = nullptr;
    QLabel *m_errTileCount = nullptr;
    QLabel *m_errTileWorst = nullptr;
    QLabel *m_errTileRate = nullptr;

    // Datadog-style Dependencies page (M61r): library resolution tiles.
    QLabel *m_depTileLibs = nullptr;
    QLabel *m_depTilePkg = nullptr;
    QLabel *m_depTileUnmapped = nullptr;
    QLabel *m_depTileRatio = nullptr;

    // Datadog-style Software page (M61q): SBOM inventory tiles.
    QLabel *m_sweTilePkgs = nullptr;
    QLabel *m_sweTileLibs = nullptr;
    QLabel *m_sweTileResolved = nullptr;
    QLabel *m_sweTileFams = nullptr;

    // Datadog-style Endpoints/Synthetic/Instrumentation pages (M61s-u).
    QLabel *m_epTileTotal = nullptr;
    QLabel *m_epTilePublic = nullptr;
    QLabel *m_epTileLocal = nullptr;
    QLabel *m_epTileDb = nullptr;
    QLabel *m_synTileChecks = nullptr;
    QLabel *m_synTileUp = nullptr;
    QLabel *m_synTileDown = nullptr;
    QLabel *m_synTileAvg = nullptr;
    TimeseriesChart *m_chartSynthetic = nullptr;
    QLabel *m_insTileAgents = nullptr;
    QLabel *m_insTileDd = nullptr;
    QLabel *m_insTileOtel = nullptr;
    QLabel *m_insTileOther = nullptr;

    // Datadog-style Traces/Profiles/Databases/Security pages (M61v-y).
    QLabel *m_traceTileTraces = nullptr;
    QLabel *m_traceTileSpans = nullptr;
    QLabel *m_traceTileErrors = nullptr;
    QLabel *m_traceTileServices = nullptr;
    TimeseriesChart *m_chartTrace = nullptr;
    QLabel *m_profTileThreads = nullptr;
    QLabel *m_profTileCpu = nullptr;
    QLabel *m_profTileOffCpu = nullptr;
    QLabel *m_profTilePss = nullptr;
    TimeseriesChart *m_chartProfile = nullptr;
    QLabel *m_dbTileCache = nullptr;
    QLabel *m_dbTileCommits = nullptr;
    QLabel *m_dbTileRollbacks = nullptr;
    QLabel *m_dbTileDeadlocks = nullptr;
    TimeseriesChart *m_chartDb = nullptr;
    QLabel *m_secTileExposure = nullptr;
    QLabel *m_secTileFindings = nullptr;
    QLabel *m_secTileThreats = nullptr;
    QLabel *m_secTileFim = nullptr;

    QListWidget *m_rail = nullptr;
    QStackedWidget *m_views = nullptr;

    QSet<int> m_collapsedPids;
    bool m_populating = false;
    SettingsInfo m_settings;
    QLabel *m_settingsSoftware = nullptr;
    QLabel *m_settingsGeneral = nullptr;
    QLabel *m_settingsDiagnostics = nullptr;
    QHash<QString, QDoubleSpinBox *> m_monitorThreshold;
    QHash<QString, QSpinBox *> m_monitorCycles;
};
