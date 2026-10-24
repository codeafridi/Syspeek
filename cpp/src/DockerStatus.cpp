#include "DockerStatus.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace {

QString parseHealth(const QString &status) {
    const QString lower = status.toLower();
    if (lower.contains(QStringLiteral("(unhealthy)"))) {
        return QStringLiteral("unhealthy");
    }
    if (lower.contains(QStringLiteral("health: starting"))
        || lower.contains(QStringLiteral("(starting)"))) {
        return QStringLiteral("starting");
    }
    if (lower.contains(QStringLiteral("(healthy)"))) {
        return QStringLiteral("healthy");
    }
    return QString();
}

QString issueText(int issues) {
    return issues == 1 ? QStringLiteral("issue") : QStringLiteral("issues");
}

void applyDockerRowSemantics(DockerContainer *container) {
    container->health = parseHealth(container->status);
    container->exitCode = parseDockerExitCode(container->status);
}

} // namespace

int parseDockerExitCode(const QString &status) {
    const int open = status.indexOf(QLatin1Char('('));
    if (open < 0) {
        return -1;
    }
    const int close = status.indexOf(QLatin1Char(')'), open + 1);
    if (close < 0) {
        return -1;
    }

    bool ok = false;
    const int code =
        status.mid(open + 1, close - open - 1).trimmed().toInt(&ok);
    return ok ? code : -1;
}

QList<DockerContainer> parseDockerPs(const QByteArray &payload, bool *ok) {
    if (ok != nullptr) {
        *ok = true;
    }

    QList<DockerContainer> containers;
    const QList<QByteArray> lines = payload.split('\n');

    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }

        const QList<QByteArray> fields = line.split('\t');
        if (fields.size() != 4) {
            if (ok != nullptr) {
                *ok = false;
            }
            return {};
        }

        DockerContainer container;
        container.id = QString::fromUtf8(fields.at(0)).trimmed();
        container.name = QString::fromUtf8(fields.at(1)).trimmed();
        container.state = QString::fromUtf8(fields.at(2)).trimmed();
        container.status = QString::fromUtf8(fields.at(3)).trimmed();
        container.health = parseHealth(container.status);
        container.exitCode = parseDockerExitCode(container.status);
        containers.append(container);
    }

    return containers;
}

bool dockerContainerIsIssue(const DockerContainer &container) {
    if (container.crashed) {
        return true;
    }
    if (container.state.compare(QStringLiteral("restarting"),
                                Qt::CaseInsensitive)
        == 0) {
        return true;
    }
    return container.health == QStringLiteral("unhealthy");
}

int dockerRunningCount(const QList<DockerContainer> &containers) {
    int running = 0;
    for (const DockerContainer &container : containers) {
        if (container.state.compare(QStringLiteral("running"),
                                    Qt::CaseInsensitive)
            == 0) {
            ++running;
        }
    }
    return running;
}

int dockerIssueCount(const QList<DockerContainer> &containers) {
    int issues = 0;
    for (const DockerContainer &container : containers) {
        if (dockerContainerIsIssue(container)) {
            ++issues;
        }
    }
    return issues;
}

QList<DockerContainer> parseDockerContainersJson(const QByteArray &payload,
                                                 QString *error) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isArray()) {
        if (error != nullptr) {
            *error = QStringLiteral("engine response is not a container list");
        }
        return {};
    }

    QList<DockerContainer> containers;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        DockerContainer container;
        container.id = obj.value(QLatin1String("Id")).toString();
        if (container.id.isEmpty()) {
            continue;
        }
        const QJsonArray names = obj.value(QLatin1String("Names")).toArray();
        if (!names.isEmpty()) {
            container.name = names.at(0).toString();
            while (container.name.startsWith(QLatin1Char('/'))) {
                container.name = container.name.mid(1);
            }
        }
        container.image = obj.value(QLatin1String("Image")).toString();
        container.state = obj.value(QLatin1String("State")).toString();
        container.status = obj.value(QLatin1String("Status")).toString();
        applyDockerRowSemantics(&container);
        containers.append(container);
    }

    return containers;
}

bool parseDockerStatsJson(const QByteArray &payload, DockerDepth *out) {
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.contains(QLatin1String("message"))) {
        return false;
    }

    DockerDepth depth;

    const QJsonObject cpu = root.value(QLatin1String("cpu_stats")).toObject();
    const QJsonObject preCpu =
        root.value(QLatin1String("precpu_stats")).toObject();
    const QJsonObject cpuUsage =
        cpu.value(QLatin1String("cpu_usage")).toObject();
    const QJsonObject preCpuUsage =
        preCpu.value(QLatin1String("cpu_usage")).toObject();
    const double total =
        cpuUsage.value(QLatin1String("total_usage")).toDouble();
    const double prevTotal =
        preCpuUsage.value(QLatin1String("total_usage")).toDouble();
    const double system = cpu.value(QLatin1String("system_cpu_usage")).toDouble();
    const double prevSystem =
        preCpu.value(QLatin1String("system_cpu_usage")).toDouble();
    depth.cpuCores = cpu.value(QLatin1String("online_cpus")).toInt();

    const double cpuDelta = total - prevTotal;
    const double systemDelta = system - prevSystem;
    if (systemDelta > 0.0 && cpuDelta >= 0.0 && depth.cpuCores > 0) {
        depth.cpuPercent =
            (cpuDelta / systemDelta) * static_cast<double>(depth.cpuCores)
            * 100.0;
    }

    const QJsonObject mem = root.value(QLatin1String("memory_stats")).toObject();
    if (mem.contains(QLatin1String("usage"))) {
        depth.memUsedBytes =
            static_cast<unsigned long long>(
                mem.value(QLatin1String("usage")).toDouble());
        depth.memLimitBytes =
            static_cast<unsigned long long>(
                mem.value(QLatin1String("limit")).toDouble());
        depth.hasMem = depth.memUsedBytes > 0;
    }

    const QJsonObject networks =
        root.value(QLatin1String("networks")).toObject();
    if (!networks.isEmpty()) {
        unsigned long long rx = 0;
        unsigned long long tx = 0;
        bool any = false;
        for (QJsonObject::const_iterator it = networks.constBegin();
             it != networks.constEnd(); ++it) {
            const QJsonObject iface = it.value().toObject();
            rx += static_cast<unsigned long long>(
                iface.value(QLatin1String("rx_bytes")).toDouble());
            tx += static_cast<unsigned long long>(
                iface.value(QLatin1String("tx_bytes")).toDouble());
            any = true;
        }
        if (any) {
            depth.hasNet = true;
            depth.netRxBytes = rx;
            depth.netTxBytes = tx;
        }
    }

    const QJsonObject blkio =
        root.value(QLatin1String("blkio_stats")).toObject();
    const QJsonArray ioBytes =
        blkio.value(QLatin1String("io_service_bytes_recursive")).toArray();
    if (!ioBytes.isEmpty()) {
        unsigned long long read = 0;
        unsigned long long write = 0;
        for (const QJsonValue &entry : ioBytes) {
            const QJsonObject item = entry.toObject();
            const QString op =
                item.value(QLatin1String("op")).toString().toLower();
            const unsigned long long value =
                static_cast<unsigned long long>(
                    item.value(QLatin1String("value")).toDouble());
            if (op == QLatin1String("read")) {
                read += value;
            } else if (op == QLatin1String("write")) {
                write += value;
            }
        }
        depth.hasBlk = read > 0 || write > 0;
        depth.blkReadBytes = read;
        depth.blkWriteBytes = write;
    }

    const QJsonObject pids = root.value(QLatin1String("pids_stats")).toObject();
    if (pids.contains(QLatin1String("current"))) {
        depth.pids = pids.value(QLatin1String("current")).toInt();
    }

    if (out != nullptr) {
        *out = depth;
    }
    return true;
}

DockerState classifyDockerFailure(const QByteArray &payload) {
    const QByteArray lower = payload.toLower();

    if (lower.contains("permission denied") || lower.contains("access denied")) {
        return DockerState::PermissionDenied;
    }

    if (lower.contains("cannot connect") || lower.contains("failed to connect")
        || lower.contains("is the docker daemon running")
        || lower.contains("no such file or directory")) {
        return DockerState::Unreachable;
    }

    return DockerState::CommandFailed;
}

QString dockerSummaryText(const DockerSnapshot &snapshot) {
    switch (snapshot.state) {
    case DockerState::NotInstalled:
        // M58: silent-empty card told users nothing. Naming the missing
        // dependency is the honest state (the docker CLI ships with
        // Docker Engine / Docker Desktop).
        return QStringLiteral(
            "docker: not installed - install Docker Engine or Docker "
            "Desktop to see containers");
    case DockerState::Starting:
        return QStringLiteral("docker: starting");
    case DockerState::PermissionDenied:
        return QStringLiteral("docker: permission denied");
    case DockerState::Unreachable:
        return QStringLiteral("docker: daemon unreachable");
    case DockerState::TimedOut:
        return QStringLiteral("docker: timed out");
    case DockerState::CommandFailed:
        return QStringLiteral("docker: command failed");
    case DockerState::ParseFailed:
        return QStringLiteral("docker: parse failed");
    case DockerState::Stale:
        break;
    case DockerState::Ok:
    default:
        break;
    }

    const int running = snapshot.runningCount;
    const int issues = snapshot.issueCount;

    QString text = QStringLiteral("docker: %1 running").arg(running);
    if (issues > 0) {
        text += QStringLiteral(" | %1 %2").arg(issues).arg(issueText(issues));
    }
    if (snapshot.state == DockerState::Stale || snapshot.stale) {
        text += QStringLiteral(" | stale");
    }
    return text;
}

QString dockerDetailText(const DockerSnapshot &snapshot) {
    QStringList lines;

    for (const DockerContainer &container : snapshot.containers) {
        if (!dockerContainerIsIssue(container)) {
            continue;
        }

        QString detail = QStringLiteral("- %1").arg(container.name);
        if (!container.state.isEmpty()) {
            detail += QStringLiteral(" | %1").arg(container.state);
        }
        if (!container.status.isEmpty()) {
            detail += QStringLiteral(" | %1").arg(container.status);
        }
        if (container.crashed) {
            detail += QStringLiteral(" | recently exited");
        }
        lines.append(detail);
    }

    if (lines.isEmpty()) {
        return QStringLiteral("no issues");
    }

    return lines.join(QLatin1Char('\n'));
}

void DockerStateTracker::update(QList<DockerContainer> &containers) {
    QSet<QString> present;

    for (DockerContainer &container : containers) {
        present.insert(container.id);

        const QString previous = m_previousState.value(container.id);
        if (previous == QStringLiteral("running")
            && container.state == QStringLiteral("exited")
            && container.exitCode != 0) {
            m_crashed.insert(container.id);
        }

        if (container.state == QStringLiteral("running")) {
            m_crashed.remove(container.id);
        }

        container.crashed = m_crashed.contains(container.id);
    }

    for (auto it = m_crashed.begin(); it != m_crashed.end();) {
        if (present.contains(*it)) {
            ++it;
        } else {
            it = m_crashed.erase(it);
        }
    }

    QHash<QString, QString> next;
    for (const DockerContainer &container : containers) {
        next.insert(container.id, container.state);
    }
    m_previousState = next;
}

