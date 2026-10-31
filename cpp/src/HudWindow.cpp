#include "HudWindow.h"

#include "DashboardPanel.h"
#include "Theme.h"
#include "platform/PlatformFactory.h"

#include "platform/linux/LinuxOverlay.h"

#ifdef Q_OS_MACOS
#include "platform/mac/MacOverlay.h"
#endif

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>

#include <cmath>

namespace {

QFont valueFont() {
    QFont font;
    font.setStyleHint(QFont::Monospace);
    font.setFamily(QStringLiteral("monospace"));
    font.setPointSize(10);
    font.setBold(true);
    return font;
}

QFont captionFont() {
    QFont font;
    font.setPointSize(9);
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    return font;
}

QString statusColor(const QString &status) {
    return ui_theme::statusColor(status);
}

} // namespace

HudWindow::HudWindow(QWidget *parent)
    : QWidget(parent) {
    setWindowTitle(QStringLiteral("Syspeek"));
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                   | Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
#ifdef Q_OS_LINUX
    setAttribute(Qt::WA_X11DoNotAcceptFocus, true);
#endif
    setObjectName(QStringLiteral("hudRoot"));

    m_webhook = new AlertWebhook(this);
    connect(m_webhook, &AlertWebhook::deliveryFinished, this,
            [this](bool, const QString &) {
                if (m_panel != nullptr) {
                    m_panel->setWebhookLine(m_webhook->statusLine());
                }
            });

    m_pages = new QWidget(this);
    auto *pagesLayout = new QVBoxLayout(m_pages);
    pagesLayout->setContentsMargins(0, 0, 0, 0);
    pagesLayout->setSpacing(0);
    m_compactPage = new QWidget(m_pages);
    auto *root = new QVBoxLayout(m_compactPage);
    root->setContentsMargins(14, 10, 14, 16);
    root->setSpacing(10);

    auto *header = new QHBoxLayout();
    header->setSpacing(6);

    auto *title = new QLabel(QStringLiteral("SYSPEEK"), this);
    title->setObjectName(QStringLiteral("title"));
    title->setFont(captionFont());

    m_statusValue = makeValue();
    m_statusValue->setObjectName(QStringLiteral("status"));

    header->addWidget(title);
    header->addStretch(1);
    header->addWidget(m_statusValue);
    root->addLayout(header);

    auto *metrics = new QHBoxLayout();
    metrics->setSpacing(18);

    m_appValue = makeValue();
    m_appValue->setObjectName(QStringLiteral("appValue"));
    m_pidValue = makeValue();
    m_pidValue->setObjectName(QStringLiteral("pidValue"));
    m_cpuValue = makeValue();
    m_cpuValue->setObjectName(QStringLiteral("cpuValue"));
    m_memoryValue = makeValue();
    m_memoryValue->setObjectName(QStringLiteral("memoryValue"));

    metrics->addLayout(makeMetricBlock(QStringLiteral("App"), m_appValue));
    metrics->addLayout(makeMetricBlock(QStringLiteral("PID"), m_pidValue));
    metrics->addLayout(makeMetricBlock(QStringLiteral("CPU"), m_cpuValue));
    metrics->addLayout(makeMetricBlock(QStringLiteral("MEM (RSS)"), m_memoryValue));
    metrics->addStretch(1);

    m_metricsWidget = new QWidget(this);
    m_metricsWidget->setObjectName(QStringLiteral("metrics"));
    m_metricsWidget->setLayout(metrics);
    root->addWidget(m_metricsWidget);

    m_compactLabel = new QLabel(this);
    m_compactLabel->setObjectName(QStringLiteral("compact"));
    m_compactLabel->setFont(valueFont());
    m_compactLabel->setVisible(false);
    root->addWidget(m_compactLabel);

    m_dockerButton = new QPushButton(this);
    m_dockerButton->setObjectName(QStringLiteral("dockerButton"));
    m_dockerButton->setFont(valueFont());
    m_dockerButton->setFocusPolicy(Qt::NoFocus);
    m_dockerButton->setCursor(Qt::PointingHandCursor);
    m_dockerButton->setVisible(false);
    connect(m_dockerButton, &QPushButton::clicked, this,
            &HudWindow::toggleDockerDetails);
    root->addWidget(m_dockerButton);

    m_dockerDetails = new QLabel(this);
    m_dockerDetails->setObjectName(QStringLiteral("dockerDetails"));
    m_dockerDetails->setFont(valueFont());
    m_dockerDetails->setWordWrap(true);
    m_dockerDetails->setVisible(false);
    root->addWidget(m_dockerDetails);

    m_kubeButton = new QPushButton(this);
    m_kubeButton->setObjectName(QStringLiteral("kubeButton"));
    m_kubeButton->setFont(valueFont());
    m_kubeButton->setFocusPolicy(Qt::NoFocus);
    m_kubeButton->setCursor(Qt::PointingHandCursor);
    m_kubeButton->setVisible(false);
    connect(m_kubeButton, &QPushButton::clicked, this,
            &HudWindow::toggleKubeDetails);
    root->addWidget(m_kubeButton);

    m_kubeDetails = new QLabel(this);
    m_kubeDetails->setObjectName(QStringLiteral("kubeDetails"));
    m_kubeDetails->setFont(valueFont());
    m_kubeDetails->setWordWrap(true);
    m_kubeDetails->setVisible(false);
    root->addWidget(m_kubeDetails);

    auto *footer = new QHBoxLayout();
    footer->setSpacing(4);
    footer->addStretch(1);

    m_moreButton = new QPushButton(QStringLiteral("MORE"), this);
    m_moreButton->setObjectName(QStringLiteral("moreButton"));
    m_moreButton->setFocusPolicy(Qt::NoFocus);
    m_moreButton->setCursor(Qt::PointingHandCursor);
    connect(m_moreButton, &QPushButton::clicked, this, &HudWindow::toggleDetails);

    m_showHideButton = new QPushButton(QStringLiteral("HIDE"), this);
    m_showHideButton->setObjectName(QStringLiteral("showHideButton"));
    m_showHideButton->setFocusPolicy(Qt::NoFocus);
    m_showHideButton->setCursor(Qt::PointingHandCursor);
    connect(m_showHideButton, &QPushButton::clicked, this, &HudWindow::toggleCompact);

    m_clickButton = new QPushButton(QStringLiteral("CLICK"), this);
    m_clickButton->setObjectName(QStringLiteral("clickButton"));
    m_clickButton->setFocusPolicy(Qt::NoFocus);
    m_clickButton->setCursor(Qt::PointingHandCursor);
    m_clickButton->setToolTip(QStringLiteral(
        "click-through: let every click except this button reach the window "
        "below"));
    connect(m_clickButton, &QPushButton::clicked, this,
            &HudWindow::toggleClickThrough);

    // Geometry is remembered from the signals the window actually emits rather
    // than from the values we asked for: a layout can grow a frame the moment
    // it is laid out (wider fonts, a longer monitor name) and a resize we
    // requested would then be persisted as a size the window does not have.
    // Debounced so a drag does not write the file on every mouse move.
    m_overlaySaveTimer = new QTimer(this);
    m_overlaySaveTimer->setSingleShot(true);
    m_overlaySaveTimer->setInterval(400);
    connect(m_overlaySaveTimer, &QTimer::timeout, this,
            &HudWindow::persistOverlay);

    footer->addWidget(m_moreButton);
    footer->addWidget(m_clickButton);
    footer->addWidget(m_showHideButton);
    root->addLayout(footer);

    for (QPushButton *button : {m_dockerButton, m_kubeButton, m_moreButton,
                                m_clickButton, m_showHideButton}) {
        button->installEventFilter(this);
    }

    setStyleSheet(QStringLiteral(
        "#hudRoot {"
        "  background-color: %BG%;"
        "  border: 1px solid %BORDER%;"
        "}"
        "#title {"
        "  color: %DIM%;"
        "  background: transparent;"
        "  padding: 4px 6px 4px 12px;"
        "  letter-spacing: 1.5px;"
        "}"
        "#caption { color: %DIM%; }"
        "#dockerButton {"
        "  color: %DIM%;"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "  text-align: left;"
        "}"
        "#dockerButton:hover { color: %TEXT%; }"
        "#dockerDetails { color: %DIM%; }"
        "#kubeButton {"
        "  color: %DIM%;"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "  text-align: left;"
        "}"
        "#kubeButton:hover { color: %TEXT%; }"
        "#kubeDetails { color: %DIM%; }"
        "#status  { font-weight: bold; }"
        "#detailsPanel {"
        "  background-color: %SURFACE%;"
        "  border-left: 1px solid %BORDER%;"
        "}"
        "#moreButton, #clickButton, #showHideButton {"
        "  color: %MUTED%;"
        "  background-color: transparent;"
        "  border: 1px solid %BORDER%;"
        "  font-size: 9px;"
        "  padding: 3px 10px;"
        "}"
        "#moreButton:hover, #clickButton:hover, #showHideButton:hover {"
        "  color: %TEXT%; border-color: %MUTED%;"
        "}"
        "QLabel   { color: %DIM%; }")
        .replace(QStringLiteral("%BG%"), ui_theme::bg())
        .replace(QStringLiteral("%BORDER%"), ui_theme::border())
        .replace(QStringLiteral("%TEXT%"), ui_theme::text())
        .replace(QStringLiteral("%DIM%"), ui_theme::dim())
        .replace(QStringLiteral("%MUTED%"), ui_theme::muted())
        .replace(QStringLiteral("%SURFACE%"), ui_theme::surface()));

    m_panel = new DashboardPanel(m_pages);
    connect(m_panel, &DashboardPanel::collapseRequested, this,
            &HudWindow::toggleDetails);
    connect(m_panel, &DashboardPanel::monitorChanged, this,
            &HudWindow::applyMonitorChange);
    connect(m_panel, &DashboardPanel::monitorsResetRequested, this,
            &HudWindow::resetMonitors);
    connect(m_panel, &DashboardPanel::historyExportRequested, this,
            &HudWindow::exportHistory);
    connect(m_panel, &DashboardPanel::historySeekRequested, this,
            &HudWindow::seekHistory);
    connect(m_panel, &DashboardPanel::silenceRequested, this,
            &HudWindow::addSilence);
    connect(m_panel, &DashboardPanel::silenceExpireRequested, this,
            &HudWindow::expireSilence);
    connect(m_panel, &DashboardPanel::processActionRequested, this,
            &HudWindow::applyProcessAction);
    connect(m_panel, &DashboardPanel::serviceRestartRequested, this,
            &HudWindow::serviceRestartRequested);
    connect(m_panel, &DashboardPanel::containerActionRequested, this,
            &HudWindow::containerActionRequested);
    connect(m_panel, &DashboardPanel::timelineFocusRequested, this,
            &HudWindow::focusTimeline);
    connect(m_panel, &DashboardPanel::timelineFocusCleared, this, [this]() {
        m_timelineFocusMs = 0;
        publishTimeline();
    });
    m_panel->setVisible(false);
    pagesLayout->addWidget(m_compactPage);
    pagesLayout->addWidget(m_panel);

    auto *windowLayout = new QVBoxLayout(this);
    windowLayout->setContentsMargins(0, 0, 0, 0);
    windowLayout->setSpacing(0);
    windowLayout->addWidget(m_pages);

    setSnapshot(DisplaySnapshot{});

    setFixedWidth(430);
    adjustSize();
}

QLabel *HudWindow::makeCaption(const QString &text) {
    auto *label = new QLabel(text, this);
    label->setObjectName(QStringLiteral("caption"));
    label->setFont(captionFont());
    return label;
}

QLabel *HudWindow::makeValue() {
    auto *label = new QLabel(QStringLiteral("--"), this);
    label->setFont(valueFont());
    return label;
}

QLayout *HudWindow::makeMetricBlock(const QString &caption, QLabel *value) {
    auto *box = new QVBoxLayout();
    box->setSpacing(2);
    box->addWidget(makeCaption(caption));
    box->addWidget(value);
    return box;
}

void HudWindow::setSnapshot(const DisplaySnapshot &snapshot) {
    m_lastSnapshot = snapshot;
    m_exportRows.append(
        exportRowFrom(snapshot, QDateTime::currentMSecsSinceEpoch()));
    while (m_exportRows.size() > 1800) {
        m_exportRows.removeFirst();
    }
    if (m_historyLog.isOpen()) {
        m_historyLog.append(historyRecordFrom(m_exportRows.last()));
        publishHistory();
    }
    // The event page is republished on a slow cadence so "12s ago" stays
    // true without rebuilding rows on every tick; new events pull that
    // forward exactly once.
    ++m_timelineTick;
    if (m_timelineDirty || m_timelineTick % 5 == 0) {
        publishTimeline();
        m_timelineDirty = false;
    }

    if (m_appValue->text() != snapshot.appName) {
        m_appValue->setText(snapshot.appName);
    }
    if (m_pidValue->text() != snapshot.pid) {
        m_pidValue->setText(snapshot.pid);
    }
    if (m_cpuValue->text() != snapshot.cpu) {
        m_cpuValue->setText(snapshot.cpu);
    }
    if (m_memoryValue->text() != snapshot.memory) {
        m_memoryValue->setText(snapshot.memory);
    }
    if (m_statusValue->text() != snapshot.status) {
        m_statusValue->setText(snapshot.status);
        m_statusValue->setStyleSheet(
            QStringLiteral("color:%1;").arg(statusColor(snapshot.status)));
    }

    updateCompactText();
    if (m_panel != nullptr) {
        m_panel->setSnapshot(snapshot);
    }

    m_alertInput.appCpuValid = snapshot.cpuValid;
    m_alertInput.appCpu = snapshot.cpuPercent;
    m_alertInput.hostCpuValid = snapshot.hostAvailable && !snapshot.hostWarming;
    m_alertInput.hostCpu = snapshot.hostCpuPercent;
    if (snapshot.hostAvailable && !snapshot.hostWarming
        && snapshot.hostStealPercent >= 0.0) {
        m_alertInput.stealValid = true;
        m_alertInput.stealPercent = snapshot.hostStealPercent;
        m_stealHistory.append(snapshot.hostStealPercent);
        if (m_stealHistory.size() > 120) {
            m_stealHistory.removeFirst();
        }
    } else {
        m_alertInput.stealValid = false;
    }
    if (snapshot.sensors.available && snapshot.sensors.hottestCelsius >= 0.0) {
        m_alertInput.tempValid = true;
        m_alertInput.tempCelsius = snapshot.sensors.hottestCelsius;
    } else {
        m_alertInput.tempValid = false;
    }
    double lowestCharge = -1.0;
    for (const BatteryReading &battery : snapshot.sensors.batteries) {
        if (battery.capacityPercent < 0) {
            continue;
        }
        const double charge = static_cast<double>(battery.capacityPercent);
        lowestCharge = lowestCharge < 0.0 ? charge : qMin(lowestCharge, charge);
    }
    if (lowestCharge >= 0.0) {
        m_alertInput.batteryValid = true;
        m_alertInput.batteryPercent = lowestCharge;
    } else {
        m_alertInput.batteryValid = false;
    }
    if (snapshot.kernelLimits.available && snapshot.kernelLimits.fdMax > 0) {
        m_alertInput.kernelFdValid = true;
        m_alertInput.kernelFdPct =
            100.0
            * static_cast<double>(snapshot.kernelLimits.fdAllocated)
                  / static_cast<double>(snapshot.kernelLimits.fdMax);
    } else {
        m_alertInput.kernelFdValid = false;
    }

    if (snapshot.cpuValid) {
        m_cpuHistory.append(snapshot.cpuPercent);
        if (m_cpuHistory.size() > 120) {
            m_cpuHistory.removeFirst();
        }
    }
    if (snapshot.hostAvailable && !snapshot.hostWarming) {
        m_hostCpuHistory.append(snapshot.hostCpuPercent);
        if (m_hostCpuHistory.size() > 120) {
            m_hostCpuHistory.removeFirst();
        }
    }
    const AnomalyResult cpuAnomaly =
        detectAnomaly(m_cpuHistory, AnomalyModel::Mad, 3.5);
    const AnomalyResult hostAnomaly =
        detectAnomaly(m_hostCpuHistory, AnomalyModel::ZScore, 3.0);
    const AnomalyResult stealAnomaly =
        detectAnomaly(m_stealHistory, AnomalyModel::Mad, 3.5);
    const ForecastResult cpuForecast =
        forecastCrossing(m_cpuHistory, 90.0, 60);
    m_alertInput.watchdogValid = m_cpuHistory.size() >= 10;
    m_alertInput.watchdogScore =
        qMax(qMax(std::fabs(cpuAnomaly.score), std::fabs(hostAnomaly.score)),
             std::fabs(stealAnomaly.score));
    m_alertInput.watchdogForecastSteps =
        cpuForecast.crosses ? cpuForecast.stepsToThreshold : 0;
    if (snapshot.hostMemTotalKb > 0) {
        m_alertInput.hostMemValid = true;
        m_alertInput.hostMemFreePct =
            100.0 * snapshot.hostMemAvailableKb / snapshot.hostMemTotalKb;
    }
    if (snapshot.hostDiskTotalBytes > 0) {
        m_alertInput.diskValid = true;
        m_alertInput.diskFreePct =
            100.0 * snapshot.hostDiskFreeBytes / snapshot.hostDiskTotalBytes;
    }
    evaluateAlerts();
}

void HudWindow::setDockerSnapshot(const DockerSnapshot &snapshot) {
    const QString summary = dockerSummaryText(snapshot);
    const QString details = dockerDetailText(snapshot);
    m_dockerDetailsText = details;

    const bool wasVisible = m_dockerButton->isVisibleTo(this);

    if (m_dockerButton->text() != summary) {
        m_dockerButton->setText(summary);
    }

    const bool nowVisible = !summary.isEmpty();
    if (wasVisible != nowVisible) {
        m_dockerButton->setVisible(nowVisible);
        if (!nowVisible) {
            m_dockerOpen = false;
            m_dockerDetails->setVisible(false);
        }
        if (!m_detailsExpanded) {
            adjustSize();
        }
    }

    if (m_dockerOpen) {
        if (m_dockerDetailsText.isEmpty()) {
            m_dockerOpen = false;
            m_dockerDetails->setVisible(false);
        } else {
            m_dockerDetails->setText(m_dockerDetailsText);
            m_dockerDetails->setVisible(true);
        }
    }

    if (m_panel != nullptr) {
        m_panel->setDockerSnapshot(snapshot);
    }
}

void HudWindow::toggleDockerDetails() {
    if (!m_dockerOpen && m_dockerDetailsText.isEmpty()) {
        return;
    }

    m_dockerOpen = !m_dockerOpen;

    if (m_dockerOpen) {
        m_dockerDetails->setText(m_dockerDetailsText);
        m_dockerDetails->setVisible(true);
    } else {
        m_dockerDetails->setVisible(false);
    }

    if (!m_detailsExpanded) {
        adjustSize();
    }
}

void HudWindow::setKubeSnapshot(const KubeSnapshot &snapshot) {
    const QString summary = kubeSummaryText(snapshot);
    const QString details = kubeDetailText(snapshot);
    m_kubeDetailsText = details;

    const bool wasVisible = m_kubeButton->isVisibleTo(this);

    if (m_kubeButton->text() != summary) {
        m_kubeButton->setText(summary);
    }

    const bool nowVisible = !summary.isEmpty();
    if (wasVisible != nowVisible) {
        m_kubeButton->setVisible(nowVisible);
        if (!nowVisible) {
            m_kubeOpen = false;
            m_kubeDetails->setVisible(false);
        }
        if (!m_detailsExpanded) {
            adjustSize();
        }
    }

    if (m_kubeOpen) {
        if (m_kubeDetailsText.isEmpty()) {
            m_kubeOpen = false;
            m_kubeDetails->setVisible(false);
        } else {
            m_kubeDetails->setText(m_kubeDetailsText);
            m_kubeDetails->setVisible(true);
        }
    }

    if (m_panel != nullptr) {
        m_panel->setKubeSnapshot(snapshot);
    }
    m_alertInput.k8sValid = snapshot.state == KubeState::Ok;
    m_alertInput.k8sIssues = snapshot.issueCount + snapshot.eventCount;
    evaluateAlerts();

    int recorded = 0;
    for (const KubeChange &change : snapshot.timeline) {
        const QString key = QStringLiteral("kube:%1:%2:%3:%4")
                                .arg(change.atMs)
                                .arg(change.ns, change.object, change.summary);
        if (!eventFirstSeen(key)) {
            continue;
        }
        if (++recorded > 12) {
            break;
        }
        TimelineEvent event;
        event.kind = TimelineKind::Kubernetes;
        event.atMs = change.atMs;
        event.severity = TimelineSeverity::Info;
        event.subject = QStringLiteral("kube:%1/%2")
                            .arg(change.ns, change.object);
        event.title = change.summary;
        event.tags = {QStringLiteral("kind:%1").arg(change.kind),
                      QStringLiteral("ns:%1").arg(change.ns)};
        recordEvent(event);
    }
}

void HudWindow::setServicesSnapshot(const ServicesSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setServicesSnapshot(snapshot);
    }
    m_alertInput.servicesValid = snapshot.available;
    m_alertInput.servicesFailed = snapshot.failedCount;
    evaluateAlerts();

    int recorded = 0;
    for (const ServiceChange &change : snapshot.changes) {
        const QString key =
            QStringLiteral("unit:%1:%2:%3").arg(change.atMs).arg(change.unit,
                                                            change.summary);
        if (!eventFirstSeen(key)) {
            continue;
        }
        if (++recorded > 12) {
            break;
        }
        TimelineEvent event;
        event.kind = TimelineKind::Service;
        event.atMs = change.atMs;
        event.severity = TimelineSeverity::Warning;
        event.subject = QStringLiteral("unit:") + change.unit;
        event.title = change.summary;
        recordEvent(event);
    }
}

void HudWindow::setJournalSnapshot(const JournalSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setJournalSnapshot(snapshot);
    }
    m_alertInput.journalValid = snapshot.available;
    m_alertInput.journalErrors = snapshot.errorCount;
    evaluateAlerts();
}

void HudWindow::setWebhookConfig(const WebhookConfig &config) {
    if (m_webhook != nullptr) {
        m_webhook->setConfig(config);
    }
}

void HudWindow::setExportIntervalMs(int intervalMs) {
    if (intervalMs > 0) {
        m_exportIntervalMs = intervalMs;
    }
}

void HudWindow::exportHistory(const QString &format) {
    const QString fmt = normalizedExportFormat(format);

    ExportMeta meta;
    meta.hostname = QSysInfo::machineHostName();
    meta.platform = platformName();
    meta.version = QString::fromLatin1(SYSPEEK_APP_VERSION);
    meta.intervalMs = m_exportIntervalMs;

    const QString body = exportRows(m_exportRows, meta, fmt);

    QString dir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath();
    }
    QDir().mkpath(dir);
    const QString path =
        dir + QStringLiteral("/syspeek-history-%1.%2")
                  .arg(QDateTime::currentDateTimeUtc().toString(
                           QStringLiteral("yyyyMMdd-HHmmss")),
                       fmt);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (m_panel != nullptr) {
            m_panel->setExportLine(
                QStringLiteral("failed - could not write %1").arg(path));
        }
        return;
    }
    file.write(body.toUtf8());
    file.close();

    if (m_panel != nullptr) {
        m_panel->setExportLine(QStringLiteral("%1 - %2 samples -> %3")
                                   .arg(fmt)
                                   .arg(m_exportRows.size())
                                   .arg(path));
    }
}

void HudWindow::setSyntheticSnapshot(const SyntheticSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setSyntheticSnapshot(snapshot);
    }
    // Only transitions matter: a check that has been failing for an hour is
    // one event, not sixty.
    for (const ProbeResult &probe : snapshot.results) {
        const QString state = probe.success ? QStringLiteral("up")
                                            : QStringLiteral("down");
        const QString previous = m_checkStates.value(probe.url);
        if (previous == state) {
            continue;
        }
        m_checkStates.insert(probe.url, state);
        TimelineEvent event;
        event.kind = TimelineKind::Synthetic;
        event.atMs = probe.atMs;
        event.severity = probe.success ? TimelineSeverity::Ok
                                       : TimelineSeverity::Error;
        event.subject = QStringLiteral("check:") + probe.url;
        if (probe.success) {
            event.title = QStringLiteral("%1 up again (%2 ms)")
                              .arg(probe.url)
                              .arg(probe.totalMs, 0, 'f', 0);
        } else {
            event.title = QStringLiteral("%1 down").arg(probe.url);
            event.detail = probe.error.isEmpty()
                ? QStringLiteral("HTTP %1").arg(probe.status)
                : probe.error;
        }
        recordEvent(event);
    }
}

void HudWindow::setTraceSnapshot(const TraceSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setTraceSnapshot(snapshot);
    }
}

void HudWindow::setPathSnapshot(const PathSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setPathSnapshot(snapshot);
    }
}

void HudWindow::setStatsdSnapshot(const StatsdSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setStatsdSnapshot(snapshot);
    }
}

void HudWindow::setDbmSnapshot(const DbmSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setDbmSnapshot(snapshot);
    }
}

void HudWindow::setTlsSnapshot(const TlsSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setTlsSnapshot(snapshot);
    }
}

void HudWindow::setFimSnapshot(const FimSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setFimSnapshot(snapshot);
    }
    // The FIM snapshot replays its bounded change list on every rescan; the
    // seen set keeps one event per actual change (kind + path + instant).
    int recorded = 0;
    for (const FimChange &change : snapshot.changes) {
        const QString key = QStringLiteral("fim:%1:%2:%3")
                                .arg(int(change.kind))
                                .arg(change.path)
                                .arg(change.atMs);
        if (!eventFirstSeen(key)) {
            continue;
        }
        if (++recorded > 12) {
            break;
        }
        TimelineEvent event;
        event.kind = TimelineKind::FileChange;
        event.atMs = change.atMs;
        event.severity = change.kind == FimChangeKind::Metadata
            ? TimelineSeverity::Info
            : TimelineSeverity::Warning;
        event.subject = QStringLiteral("file:") + change.path;
        event.title = fimChangeKindText(change.kind) + QLatin1Char(' ')
            + change.path;
        if (!change.oldPath.isEmpty() && change.oldPath != change.path) {
            event.detail = QStringLiteral("from %1").arg(change.oldPath);
        }
        if (!change.hash.isEmpty()) {
            event.tags.append(QStringLiteral("sha:%1").arg(change.hash));
        }
        recordEvent(event);
    }
}

void HudWindow::setHistoryStore(const QString &path, int capacity) {
    m_historySeek = -1;
    m_historyTick = 0;
    m_historyWindowCount = -1;
    m_historySpanMs = 0;
    m_historyCapacity = capacity;
    if (capacity <= 0) {
        m_historyLog.close();
        publishHistory();
        return;
    }
    if (!m_historyLog.open(path, capacity)) {
        qWarning() << "history: could not open the ring at" << path
                   << "- rewinding stays unavailable";
    }
    publishHistory();
}

void HudWindow::publishHistory() {
    if (m_panel == nullptr) {
        return;
    }
    HistoryViewInfo info;
    info.enabled = m_historyLog.isOpen();
    info.path = m_historyLog.path();
    info.count = m_historyLog.count();
    info.capacity = m_historyLog.capacity();
    info.bytes = m_historyLog.isOpen() ? m_historyLog.fileBytes() : 0;
    info.intervalMs = m_historyLog.isOpen() ? m_historyLog.intervalMs() : 1000;
    info.spanMs = m_historySpanMs;
    // The stored window is read off disk on a slow cadence (every 5th tick
    // or when the sample count jumps): one append per tick is the only
    // steady disk traffic the history costs.
    ++m_historyTick;
    if (info.count > 0
        && (m_historyWindowCount < 0 || m_historyTick % 5 == 0
            || info.count != m_historyWindowCount)) {
        QList<HistoryRecord> window;
        m_historyLog.readLast(240, window);
        for (const HistoryRecord &record : window) {
            info.window.append(exportRowFromHistory(record));
        }
        m_historyWindowCount = info.count;
        m_historySpanMs = m_historyLog.spanMs();
        info.spanMs = m_historySpanMs;
        if (!info.window.isEmpty()) {
            info.newestMs = info.window.last().atMs;
        }
    }
    m_panel->setHistoryView(info);
    if (!info.enabled || info.count <= 0) {
        m_panel->setHistorySelection(QStringLiteral("history: disabled"),
                                     QStringLiteral("--"));
        return;
    }
    const int last = info.count - 1;
    if (m_historySeek < 0 || m_historySeek > last) {
        m_historySeek = last;
    }
    if (m_historySeek == last) {
        if (m_exportRows.isEmpty()) {
            return;
        }
        const HistoryRecord newest = historyRecordFrom(m_exportRows.last());
        m_panel->setHistorySelection(
            historyPositionText(last, info.count, newest, true),
            historyRecordText(newest));
        return;
    }
    HistoryRecord record;
    if (m_historyLog.readAt(m_historySeek, record)) {
        m_panel->setHistorySelection(
            historyPositionText(m_historySeek, info.count, record, false),
            historyRecordText(record));
    }
}

void HudWindow::seekHistory(int index) {
    if (!m_historyLog.isOpen()) {
        return;
    }
    m_historySeek = index;
    publishHistory();
}

// ---------------------------------------------------------------------------
// M86 event stream
// ---------------------------------------------------------------------------

void HudWindow::setTimelineConfig(bool enabled, int capacity, bool markers,
                                  int windowSeconds) {
    m_eventsEnabled = enabled;
    m_eventCapacity = qMax(50, capacity);
    m_eventMarkers = markers;
    m_timelineWindowMs = qint64(qMax(10, windowSeconds)) * 1000;
    while (m_events.size() > m_eventCapacity) {
        m_events.removeFirst();
    }
    publishTimeline();
}

bool HudWindow::eventFirstSeen(const QString &key) {
    if (key.isEmpty()) {
        return true;
    }
    if (m_eventSeen.contains(key)) {
        return false;
    }
    m_eventSeen.insert(key);
    // The seen set only exists to stop a resampled snapshot from replaying
    // the same change; it is bounded so a long run cannot grow it forever.
    if (m_eventSeen.size() > 8192) {
        m_eventSeen.clear();
        m_eventSeen.insert(key);
    }
    return true;
}

void HudWindow::recordEvent(const TimelineEvent &event) {
    if (!m_eventsEnabled) {
        return;
    }
    TimelineEvent copy = event;
    if (copy.atMs <= 0) {
        copy.atMs = QDateTime::currentMSecsSinceEpoch();
    }
    timelineAppend(m_events, copy, m_eventCapacity);
    if (copy.severity == TimelineSeverity::Error
        || copy.severity == TimelineSeverity::Warning) {
        qInfo().noquote() << QStringLiteral("[timeline]")
                          << timelineRowText(copy, copy.atMs);
    }
    // Publishes are coalesced into the tick: a FIM burst can produce a dozen
    // events at once, and each publish rebuilds the row list and re-pushes
    // the chart ticks for every chart.
    m_timelineDirty = true;
}

void HudWindow::noteActionEvent(TimelineKind kind, const QString &subject,
                                const QString &title, const QString &detail,
                                bool ok) {
    TimelineEvent event;
    event.kind = kind;
    event.severity = ok ? TimelineSeverity::Info : TimelineSeverity::Warning;
    event.subject = subject;
    event.title = title;
    event.detail = detail;
    event.tags = {timelineKindName(kind),
                  ok ? QStringLiteral("result:ok")
                     : QStringLiteral("result:failed")};
    recordEvent(event);
}

void HudWindow::focusTimeline(qint64 atMs) {
    m_timelineFocusMs = atMs > 0 ? atMs : 0;
    publishTimeline();
    if (m_panel != nullptr) {
        m_panel->showTimeline();
    }
}

void HudWindow::publishTimeline() {
    if (m_panel == nullptr) {
        return;
    }
    TimelineViewInfo info;
    info.enabled = m_eventsEnabled;
    info.markers = m_eventMarkers;
    info.capacity = m_eventCapacity;
    info.count = int(m_events.size());
    if (!m_events.isEmpty()) {
        info.oldestMs = m_events.constFirst().atMs;
        info.newestMs = m_events.constLast().atMs;
    }
    // Newest first for the list; the ring itself stays in arrival order.
    const int displayCap = 240;
    for (int i = m_events.size() - 1;
         i >= 0 && info.events.size() < displayCap; --i) {
        info.events.append(m_events.at(i));
    }
    if (m_timelineFocusMs > 0) {
        info.focus = true;
        info.focusMs = m_timelineFocusMs;
        info.focusWindowMs = m_timelineWindowMs;
        info.focusEvents = timelineNear(m_events, m_timelineFocusMs,
                                        m_timelineWindowMs, 200);
    }
    m_panel->setTimeline(info);
}

void HudWindow::setSettingsInfo(const SettingsInfo &info) {
    if (m_panel != nullptr) {
        m_panel->setSettingsInfo(info);
    }
}

void HudWindow::setMonitors(const QList<AlertRule> &rules) {
    for (const AlertRule &rule : rules) {
        m_alerts.setRuleThreshold(rule.id, rule.threshold);
        m_alerts.setRuleSustained(rule.id, rule.sustained);
    }
    if (m_panel != nullptr) {
        m_panel->setMonitors(rules);
    }
}

void HudWindow::applyMonitorChange(const AlertRule &rule) {
    m_alerts.setRuleThreshold(rule.id, rule.threshold);
    m_alerts.setRuleSustained(rule.id, rule.sustained);
    saveAlertOverrides(defaultConfigPath(), m_alerts.rules());
}

void HudWindow::resetMonitors() {
    m_alerts.resetRules();
    saveAlertOverrides(defaultConfigPath(), m_alerts.rules());
    setMonitors(m_alerts.rules());
}

void HudWindow::setActionsEnabled(bool enabled) {
    m_actionsEnabled = enabled;
}

void HudWindow::applyProcessAction(const ProcessAction &action) {
    if (m_panel == nullptr) {
        return;
    }
    if (!m_actionsEnabled) {
        m_panel->setActionStatus(
            QStringLiteral("%1 refused: actions are disabled in [actions]")
                .arg(processActionLabel(action)),
            false);
        return;
    }
    QString error;
    const bool ok = systemBackend().applyProcessAction(action, &error);
    const QString text = processActionResultText(action, ok, error);
    m_panel->setActionStatus(text, ok);
    noteActionEvent(TimelineKind::ProcessAction,
                    QStringLiteral("pid:%1").arg(action.pid),
                    processActionLabel(action), text, ok);
    if (ok) {
        qInfo().noquote() << QStringLiteral("[actions]") << text;
    } else {
        qWarning().noquote() << QStringLiteral("[actions]") << text;
    }
}

void HudWindow::setServiceActionStatus(const QString &text, bool ok) {
    if (m_panel != nullptr) {
        m_panel->setServiceStatus(text, ok);
    }
}

void HudWindow::setContainerActionStatus(const QString &text, bool ok) {
    if (m_panel != nullptr) {
        m_panel->setContainerStatus(text, ok);
    }
}

void HudWindow::setSilences(const QList<SilenceRule> &rules) {
    m_silences = rules;
    if (m_panel != nullptr) {
        m_panel->setSilences(m_silences);
    }
}

void HudWindow::addSilence(const QString &matcher, int minutes) {
    const QString pattern = matcher.trimmed();
    if (pattern.isEmpty() || minutes <= 0) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    SilenceRule rule;
    rule.matcher = pattern;
    rule.startsAtMs = now;
    rule.endsAtMs = now + qint64(minutes) * 60 * 1000;
    rule.author = qEnvironmentVariable("USER");
    if (rule.author.isEmpty()) {
        rule.author = qEnvironmentVariable("USERNAME");
    }
    if (rule.author.isEmpty()) {
        rule.author = QStringLiteral("local");
    }
    rule.comment = QStringLiteral("snoozed from the alerts page");

    // Re-snoozing the same matcher replaces its window instead of stacking
    // a second rule the user cannot see separately.
    QList<SilenceRule> rules;
    for (const SilenceRule &existing : m_silences) {
        if (existing.matcher.compare(pattern, Qt::CaseInsensitive) != 0) {
            rules.append(existing);
        }
    }
    rules.append(rule);
    setSilences(rules);
    saveSilences(defaultConfigPath(), m_silences);
}

void HudWindow::expireSilence(const QString &matcher) {
    const QString pattern = matcher.trimmed();
    if (pattern.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool changed = false;
    for (SilenceRule &rule : m_silences) {
        if (rule.matcher.compare(pattern, Qt::CaseInsensitive) == 0
            && rule.endsAtMs > now) {
            rule.endsAtMs = now;
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    setSilences(m_silences);
    saveSilences(defaultConfigPath(), m_silences);
}

void HudWindow::evaluateAlerts() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QList<Alert> fired = m_alerts.update(m_alertInput, now);
    const QList<Alert> active = m_alerts.active();

    // A live silence keeps the monitor evaluating (the page still shows the
    // breach) and only mutes the outbound paths: webhook delivery and
    // incident creation.
    QList<Alert> loudFired;
    for (const Alert &alert : fired) {
        if (activeSilenceFor(m_silences, alert.id, now) == nullptr) {
            loudFired.append(alert);
        }
    }
    QList<Alert> loudActive;
    for (const Alert &alert : active) {
        if (activeSilenceFor(m_silences, alert.id, now) == nullptr) {
            loudActive.append(alert);
        }
    }

    if (m_webhook != nullptr) {
        m_webhook->processEvents(loudFired, loudActive, now);
    }
    if (m_panel != nullptr) {
        m_panel->setAlerts(active);
        m_incidents.update(loudActive, now);
        m_panel->setIncidents(m_incidents.incidents());
    }

    // Event stream: the engine only reports transitions, so a fired alert is
    // exactly one event and a monitor that stops breaching is exactly one
    // recovery. A silenced breach is still a fact - it happened - so it is
    // recorded and tagged rather than hidden.
    for (const Alert &alert : fired) {
        TimelineEvent event;
        event.kind = TimelineKind::Alert;
        event.severity = timelineSeverityFromText(alert.severity);
        event.subject = QStringLiteral("monitor:") + alert.id;
        event.title = alert.title;
        event.detail = alert.detail;
        event.tags = {QStringLiteral("monitor:%1").arg(alert.id),
                      QStringLiteral("severity:%1").arg(alert.severity)};
        if (activeSilenceFor(m_silences, alert.id, now) != nullptr) {
            event.tags.append(QStringLiteral("silenced"));
        }
        recordEvent(event);
    }
    QSet<QString> activeIds;
    for (const Alert &alert : active) {
        activeIds.insert(alert.id);
    }
    for (const QString &id : m_activeAlertIds) {
        if (!activeIds.contains(id)) {
            TimelineEvent event;
            event.kind = TimelineKind::Alert;
            event.severity = TimelineSeverity::Ok;
            event.subject = QStringLiteral("monitor:") + id;
            event.title = QStringLiteral("%1 recovered").arg(id);
            event.tags = {QStringLiteral("monitor:%1").arg(id)};
            recordEvent(event);
        }
    }
    m_activeAlertIds = activeIds;
}

void HudWindow::toggleKubeDetails() {
    if (!m_kubeOpen && m_kubeDetailsText.isEmpty()) {
        return;
    }

    m_kubeOpen = !m_kubeOpen;

    if (m_kubeOpen) {
        m_kubeDetails->setText(m_kubeDetailsText);
        m_kubeDetails->setVisible(true);
    } else {
        m_kubeDetails->setVisible(false);
    }

    if (!m_detailsExpanded) {
        adjustSize();
    }
}

void HudWindow::updateCompactText() {
    if (m_compactLabel == nullptr) {
        return;
    }

    m_compactLabel->setText(QStringLiteral("%1 | PID %2 | CPU %3 | MEM(RSS) %4")
                                .arg(m_appValue->text(),
                                     m_pidValue->text(),
                                     m_cpuValue->text(),
                                     m_memoryValue->text()));
}

void HudWindow::toggleDetails() {
    if (m_pages == nullptr) {
        return;
    }

    if (!m_detailsExpanded) {
        m_savedGeometry = geometry();

        m_compactPage->setVisible(false);
        m_panel->setVisible(true);

        QScreen *screen = QGuiApplication::screenAt(frameGeometry().center());
        if (screen == nullptr) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen != nullptr) {
            const QRect avail = screen->availableGeometry();
            QSize want(avail.width() * 7 / 10, avail.height() * 7 / 10);
            if (m_overlayConfig.width >= 430 && m_overlayConfig.height >= 120) {
                // The console remembers the size the user last gave it.
                want = QSize(m_overlayConfig.width, m_overlayConfig.height);
            }
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            const QRect target = clampOverlayGeometry(
                QRect(QPoint(avail.center().x() - want.width() / 2,
                             avail.center().y() - want.height() / 2),
                      want),
                avail, QSize(430, 120));
            if (target.isValid()) {
                resize(target.size());
                move(target.topLeft());
            }
        }

        m_detailsExpanded = true;
    } else {
        m_panel->setVisible(false);
        m_compactPage->setVisible(true);
        m_detailsExpanded = false;

        setMinimumSize(430, 0);
        setMaximumSize(430, QWIDGETSIZE_MAX);
        adjustSize();
        move(m_savedGeometry.topLeft());
    }

    m_moreButton->setText(m_detailsExpanded ? QStringLiteral("LESS")
                                            : QStringLiteral("MORE"));

    if (m_overlay.clickThrough) {
        // The kept input rectangle follows the CLICK button when the layout
        // changes, so re-shape rather than leaving a stale clickable island.
        applyClickThrough(true);
    }
    persistOverlay();
}

void HudWindow::toggleCompact() {
    if (m_detailsExpanded) {
        toggleDetails();
    }

    m_compact = !m_compact;

    m_metricsWidget->setVisible(!m_compact);
    m_compactLabel->setVisible(m_compact);
    m_showHideButton->setText(m_compact ? QStringLiteral("SHOW")
                                        : QStringLiteral("HIDE"));

    if (m_compact) {
        m_moreButton->setText(QStringLiteral("MORE"));
        m_moreButton->setVisible(false);
    } else {
        m_moreButton->setVisible(true);
    }

    adjustSize();

    if (m_overlay.clickThrough) {
        applyClickThrough(true);
    }
}

void HudWindow::setOverlayConfig(const OverlayConfig &config,
                                const QString &path) {
    m_overlayConfig = config;
    m_overlayPath = path;
}

void HudWindow::refreshOverlayState() {
    m_overlaySession = detectOverlaySession(
        QGuiApplication::platformName(),
        QString::fromLocal8Bit(qgetenv("WAYLAND_DISPLAY")),
        QString::fromLocal8Bit(qgetenv("DISPLAY")));

    m_overlay.workArea = QRect();
    m_overlay.wmName.clear();
    m_overlay.pinnedAbove = (m_overlaySession != OverlaySession::X11);
    m_overlay.dockWindow = false;
    m_overlay.hidden = false;

    if (m_overlaySession == OverlaySession::X11) {
        const linuxoverlay::X11Probe probe = linuxoverlay::probe();
        m_overlay.wmName = probe.wmName;
        m_overlay.workArea = probe.workArea;
        m_overlay.clickThroughCapable = probe.display && probe.shapeAvailable;
        m_overlayStickySupported = probe.stickySupported;
        const QStringList states = linuxoverlay::windowStates(winId());
        m_overlay.pinnedAbove = overlayAboveConfirmed(states);
        m_overlay.sticky = overlaySticky(states);
        m_overlay.hidden = overlayHidden(states);
        m_overlay.dockWindow =
            overlayDockWindow(linuxoverlay::windowTypes(winId()));
    } else {
        // Every other window server keeps the frame above by construction
        // (NSFloatingWindowLevel / SetWindowPos' top-most band / the
        // compositor's layer), so nothing needs to be requested.
        m_overlay.clickThroughCapable = false;
    }

    if (!m_overlay.clickThrough) {
        m_overlay.note = overlayClickThroughNote(m_overlaySession,
                                                 m_overlay.clickThroughCapable);
    }
    publishOverlayState();
}

void HudWindow::publishOverlayState() {
    m_overlay.geometry = QRect(QPoint(x(), y()), size());
    if (m_panel != nullptr) {
        m_panel->setOverlayLine(
            overlaySummaryText(m_overlaySession, m_overlay));
    }
}

void HudWindow::restoreOverlay() {
    // The saved rectangle is only a hint: monitors go away, resolutions
    // change and a HUD restored onto a vanished screen is unreachable.
    if (m_overlayConfig.x < 0 || m_overlayConfig.y < 0) {
        return;
    }

    QList<QRect> screens;
    for (const QScreen *screen : QGuiApplication::screens()) {
        screens.append(screen->availableGeometry());
    }
    if (screens.isEmpty()) {
        return;
    }

    // In compact mode the frame is a fixed-width strip, so the saved size
    // belongs to the expanded console: clamp the launch position against the
    // size the window actually has, or an 820px-wide hint on a narrow screen
    // would pull the HUD to the left edge.
    const QSize savedSize =
        m_detailsExpanded
            ? QSize(m_overlayConfig.width > 0 ? m_overlayConfig.width : width(),
                    m_overlayConfig.height > 0 ? m_overlayConfig.height
                                               : height())
            : size();
    const QRect saved(QPoint(m_overlayConfig.x, m_overlayConfig.y), savedSize);
    const QRect target =
        restoreOverlayGeometry(saved, screens, QSize(430, 120));
    if (!target.isValid()) {
        return;
    }

    move(target.topLeft());
    if (m_detailsExpanded && m_overlayConfig.width > 0
        && m_overlayConfig.height > 0) {
        resize(target.size());
    }
}

void HudWindow::verifyOverlayStates(int attempt) {
    if (m_overlaySession != OverlaySession::X11) {
        return;
    }
    const QStringList states = linuxoverlay::windowStates(winId());
    m_overlay.pinnedAbove = overlayAboveConfirmed(states);
    m_overlay.sticky = overlaySticky(states);
    m_overlay.hidden = overlayHidden(states);
    m_overlay.dockWindow = overlayDockWindow(linuxoverlay::windowTypes(winId()));

    // A request sent from showEvent can land before the window manager has
    // finished managing the frame, in which case it is dropped. Retry twice,
    // then report whatever the wm actually did instead of assuming success.
    if (!m_overlay.sticky && m_overlayConfig.sticky && m_overlayStickySupported
        && attempt < 2) {
        linuxoverlay::requestSticky(winId(), nullptr);
        QTimer::singleShot(1000, this, [this, attempt] {
            verifyOverlayStates(attempt + 1);
        });
        return;
    }
    publishOverlayState();
}

void HudWindow::ensureOnScreen() {
    // A monitor can go away, or the frame can be dragged past the last edge,
    // and then the HUD is unreachable with no way back from the UI. Pull it
    // onto the screen it overlaps most (or the primary one when it overlaps
    // nothing at all).
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        return;
    }
    QList<QRect> areas;
    for (const QScreen *screen : screens) {
        areas.append(screen->availableGeometry());
    }
    const QRect current(QPoint(x(), y()), size());
    for (const QRect &area : areas) {
        if (area.intersects(current)) {
            return;
        }
    }
    const QRect target =
        restoreOverlayGeometry(current, areas, QSize(430, 120));
    if (!target.isValid()) {
        return;
    }
    move(target.topLeft());
    persistOverlay();
}

void HudWindow::persistOverlay() {
    if (m_overlayPath.isEmpty()) {
        return;
    }
    const QRect current = geometry();
    if (current == m_overlayPersisted) {
        return;
    }
    m_overlayPersisted = current;

    OverlayConfig next = m_overlayConfig;
    next.clickThrough = m_overlay.clickThrough;
    next.x = current.x();
    next.y = current.y();
    if (m_detailsExpanded) {
        next.width = current.width();
        next.height = current.height();
    }
    saveOverlayConfig(m_overlayPath, next);
    m_overlayConfig = next;
}

void HudWindow::applyClickThrough(bool on) {
    QString note;
    bool applied = false;

    if (m_overlaySession == OverlaySession::X11) {
        // The CLICK button itself stays inside the input region, otherwise
        // the mode would be a one-way trip with no way back from the HUD.
        QRect keep(QPoint(0, 0), m_clickButton->size());
        keep.moveTo(m_clickButton->mapTo(this, QPoint(0, 0)));
        keep.adjust(-4, -4, 4, 4);
        applied = linuxoverlay::setClickThrough(winId(), on, keep, &note);
        if (!applied && note.isEmpty()) {
            note = QStringLiteral("x11: the shape request was refused");
        }
    } else {
        note = overlayClickThroughNote(m_overlaySession,
                                      m_overlay.clickThroughCapable);
    }

    m_overlay.clickThrough = on && applied;
    m_overlay.note = note;
    m_overlay.geometry = QRect(QPoint(x(), y()), size());
    m_clickButton->setText(m_overlay.clickThrough ? QStringLiteral("CLICK+")
                                                 : QStringLiteral("CLICK"));
    m_clickButton->setToolTip(overlayClickThroughNote(
        m_overlaySession, m_overlay.clickThroughCapable));

    if (m_overlaySession == OverlaySession::X11 && !applied) {
        m_clickButton->setText(QStringLiteral("CLICK"));
        m_overlay.clickThrough = false;
    }

    publishOverlayState();
    persistOverlay();
}

void HudWindow::toggleClickThrough() {
    if (m_overlaySession == OverlaySession::Unknown) {
        refreshOverlayState();
    }
    applyClickThrough(!m_overlay.clickThrough);
}

void HudWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
#ifdef Q_OS_MACOS
    applyMacOverlay(this);
#endif

    if (m_overlayRestored) {
        return;
    }
    m_overlayRestored = true;
    m_overlayPersisted = geometry();

    restoreOverlay();
    refreshOverlayState();

    if (m_overlaySession == OverlaySession::X11) {
        // Qt marks the window top-most through the WM as well, but not every
        // window manager honours the initial hint; verify and ask once.
        if (!m_overlay.pinnedAbove && !m_overlayAboveRequested) {
            m_overlayAboveRequested = true;
            linuxoverlay::requestAbove(winId(), nullptr);
        }
        // A HUD that vanishes when the user switches desktop is useless, so
        // ask for sticky once and report what the wm actually did.
        if (m_overlayConfig.sticky && m_overlayStickySupported
            && !m_overlay.sticky) {
            linuxoverlay::requestSticky(winId(), nullptr);
        }
        QTimer::singleShot(250, this, [this] { verifyOverlayStates(0); });
    }

    QTimer *overlayTimer = new QTimer(this);
    overlayTimer->setInterval(1000);
    connect(overlayTimer, &QTimer::timeout, this, [this] {
        ensureOnScreen();
        persistOverlay();
        m_overlay.geometry = QRect(QPoint(x(), y()), size());
    });
    overlayTimer->start();

    // The saved mode is applied only after the window exists, and only when
    // the session can actually honour it.
    if (m_overlayConfig.clickThrough) {
        applyClickThrough(true);
    }
}

void HudWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (m_overlaySaveTimer != nullptr && m_overlayRestored) {
        m_overlaySaveTimer->start();
    }
}

void HudWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    if (m_overlaySaveTimer != nullptr && m_overlayRestored) {
        m_overlaySaveTimer->start();
    }
}

void HudWindow::keyPressEvent(QKeyEvent *event) {
    const bool quitChord =
        event->key() == Qt::Key_Q &&
        event->modifiers().testFlag(Qt::ControlModifier);

    if (event->key() == Qt::Key_Escape || quitChord) {
        close();
        return;
    }

    QWidget::keyPressEvent(event);
}

void HudWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (windowHandle() != nullptr && windowHandle()->startSystemMove()) {
            event->accept();
            return;
        }

        m_dragging = true;
        m_dragOffset =
            event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void HudWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging && event->buttons().testFlag(Qt::LeftButton)) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void HudWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

bool HudWindow::eventFilter(QObject *watched, QEvent *event) {
    if (qobject_cast<QPushButton *>(watched) == nullptr) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            m_buttonPressGlobal = mouseEvent->globalPosition().toPoint();
            m_buttonDragPending = true;
            m_buttonDragActive = false;
            m_buttonManualMove = false;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_buttonDragPending && !m_buttonDragActive
            && mouseEvent->buttons().testFlag(Qt::LeftButton)) {
            const int distance =
                (mouseEvent->globalPosition().toPoint() - m_buttonPressGlobal)
                    .manhattanLength();
            if (distance >= QApplication::startDragDistance()) {
                m_buttonDragActive = true;
                m_buttonManualMove = true;
                m_dragOffset = m_buttonPressGlobal - frameGeometry().topLeft();
                if (windowHandle() != nullptr
                    && windowHandle()->startSystemMove()) {
                    m_buttonManualMove = false;
                }
            }
        }
        if (m_buttonDragActive) {
            if (m_buttonManualMove) {
                move(mouseEvent->globalPosition().toPoint() - m_dragOffset);
            }
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        const bool wasActive = m_buttonDragActive;
        m_buttonDragPending = false;
        m_buttonDragActive = false;
        m_buttonManualMove = false;
        if (wasActive) {
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}
