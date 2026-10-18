#include "HudWindow.h"

#include "DashboardPanel.h"
#include "platform/NotifierFactory.h"

#ifdef Q_OS_MACOS
#include "platform/mac/MacOverlay.h"
#endif

#include <QApplication>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QStringList>
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
    font.setPointSize(10);
    font.setBold(true);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    return font;
}

QString statusColor(const QString &status) {
    if (status == QStringLiteral("HIGH CPU")) {
        return QStringLiteral("#ff6b6b");
    }
    if (status == QStringLiteral("WARN")) {
        return QStringLiteral("#ffd45d");
    }
    if (status == QStringLiteral("OK")) {
        return QStringLiteral("#79ffb0");
    }
    return QStringLiteral("#bbcfcd");
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

    m_notifier = createNotifier();

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

    footer->addWidget(m_moreButton);
    footer->addWidget(m_showHideButton);
    root->addLayout(footer);

    for (QPushButton *button : {m_dockerButton, m_kubeButton, m_moreButton,
                                m_showHideButton}) {
        button->installEventFilter(this);
    }

    setStyleSheet(QStringLiteral(
        "#hudRoot {"
        "  background-color:rgb(19, 19, 20);"
        "  border: 2px solidrgb(13, 13, 14);"
        "}"
        "#title {"
        "  color: #f4f8ff;"
        "  background-color:rgb(124, 128, 134);"
        "  padding: 3px 8px;"
        "}"
        "#caption { color:rgb(200, 194, 194); }"
        "#dockerButton {"
        "  color:rgb(150, 180, 220);"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "  text-align: left;"
        "}"
        "#dockerButton:hover { color:rgb(200, 220, 255); }"
        "#dockerDetails { color:rgb(150, 180, 220); }"
        "#kubeButton {"
        "  color:rgb(190, 170, 230);"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0px;"
        "  text-align: left;"
        "}"
        "#kubeButton:hover { color:rgb(220, 205, 255); }"
        "#kubeDetails { color:rgb(190, 170, 230); }"
        "#status  { color:rgb(187, 207, 205); }"
        "#detailsPanel {"
        "  background-color:rgb(28, 28, 30);"
        "  border: 1px solid rgb(70, 70, 74);"
        "}"
        "#moreButton, #showHideButton {"
        "  color: #f4f8ff;"
        "  background-color:rgb(70, 74, 82);"
        "  border: 1px solid rgb(120, 124, 132);"
        "  font-size: 10px;"
        "  padding: 2px 8px;"
        "}"
        "#moreButton:hover, #showHideButton:hover { background-color:rgb(96, 100, 110); }"
        "QLabel   { color:rgb(198, 205, 195); }"));

    m_panel = new DashboardPanel(m_pages);
    connect(m_panel, &DashboardPanel::collapseRequested, this,
            &HudWindow::toggleDetails);
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
    const ForecastResult cpuForecast =
        forecastCrossing(m_cpuHistory, 90.0, 60);
    m_alertInput.watchdogValid = m_cpuHistory.size() >= 10;
    m_alertInput.watchdogScore =
        qMax(std::fabs(cpuAnomaly.score), std::fabs(hostAnomaly.score));
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
}

void HudWindow::setServicesSnapshot(const ServicesSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setServicesSnapshot(snapshot);
    }
    m_alertInput.servicesValid = snapshot.available;
    m_alertInput.servicesFailed = snapshot.failedCount;
    evaluateAlerts();
}

void HudWindow::setJournalSnapshot(const JournalSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setJournalSnapshot(snapshot);
    }
    m_alertInput.journalValid = snapshot.available;
    m_alertInput.journalErrors = snapshot.errorCount;
    evaluateAlerts();
}

void HudWindow::setNotifier(std::unique_ptr<Notifier> notifier) {
    m_notifier = std::move(notifier);
}

void HudWindow::setSyntheticSnapshot(const SyntheticSnapshot &snapshot) {
    if (m_panel != nullptr) {
        m_panel->setSyntheticSnapshot(snapshot);
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
}

void HudWindow::evaluateAlerts() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QList<Alert> fired = m_alerts.update(m_alertInput, now);
    if (m_notifier != nullptr) {
        for (const Alert &alert : fired) {
            m_notifier->send(alert);
        }
    }
    const QList<Alert> active = m_alerts.active();
    if (m_panel != nullptr) {
        m_panel->setAlerts(active);
        m_incidents.update(active, now);
        m_panel->setIncidents(m_incidents.incidents());
    }
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
            const int w = avail.width() * 7 / 10;
            const int h = avail.height() * 7 / 10;
            setMinimumSize(0, 0);
            setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            resize(w, h);
            move(avail.center().x() - w / 2, avail.center().y() - h / 2);
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
}

void HudWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
#ifdef Q_OS_MACOS
    applyMacOverlay(this);
#endif
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
