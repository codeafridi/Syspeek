#include "KubeStatus.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace {

QString badReason(const KubePod &pod) {
    static const QStringList bad = {
        QStringLiteral("CrashLoopBackOff"),
        QStringLiteral("ImagePullBackOff"),
        QStringLiteral("ErrImagePull"),
        QStringLiteral("CreateContainerConfigError"),
        QStringLiteral("CreateContainerError"),
        QStringLiteral("InvalidImageName"),
        QStringLiteral("RunContainerError"),
        QStringLiteral("OOMKilled"),
        QStringLiteral("Error"),
    };
    return bad.contains(pod.reason) ? pod.reason : QString();
}

} // namespace

double kubeParseCpuCores(const QString &quantity) {
    const QString q = quantity.trimmed();
    if (q.isEmpty()) {
        return 0.0;
    }
    const QChar last = q.at(q.size() - 1);
    bool ok = false;
    if (last == QLatin1Char('m')) {
        const double v = q.left(q.size() - 1).toDouble(&ok);
        return ok ? v / 1000.0 : 0.0;
    }
    if (last == QLatin1Char('n')) {
        const double v = q.left(q.size() - 1).toDouble(&ok);
        return ok ? v / 1e9 : 0.0;
    }
    if (last == QLatin1Char('u')) {
        const double v = q.left(q.size() - 1).toDouble(&ok);
        return ok ? v / 1e6 : 0.0;
    }
    const double v = q.toDouble(&ok);
    return ok ? v : 0.0;
}

double kubeParseMemoryBytes(const QString &quantity) {
    const QString q = quantity.trimmed();
    if (q.isEmpty()) {
        return 0.0;
    }

    static const char *const binary[] = {"Ki", "Mi", "Gi", "Ti", "Pi", "Ei"};
    for (int i = 6; i >= 1; --i) {
        if (q.endsWith(QLatin1String(binary[i - 1]))) {
            bool ok = false;
            const double v = q.left(q.size() - 2).toDouble(&ok);
            return ok ? v * std::pow(1024.0, i) : 0.0;
        }
    }

    struct DecimalSuffix {
        const char *s;
        int power;
    };
    static const DecimalSuffix decimal[] = {{"E", 18}, {"P", 15}, {"T", 12},
                                            {"G", 9},  {"M", 6},  {"K", 3},
                                            {"k", 3}};
    for (const DecimalSuffix &entry : decimal) {
        if (q.endsWith(QLatin1String(entry.s))) {
            bool ok = false;
            const double v = q.left(q.size() - 1).toDouble(&ok);
            return ok ? v * std::pow(10.0, entry.power) : 0.0;
        }
    }

    bool ok = false;
    const double v = q.toDouble(&ok);
    return ok ? v : 0.0;
}

namespace {

KubeNode parseNode(const QJsonObject &item) {
    KubeNode node;
    const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    node.name = metadata.value(QStringLiteral("name")).toString();
    const QJsonObject labels = metadata.value(QStringLiteral("labels")).toObject();
    node.instanceType =
        labels.value(QStringLiteral("node.kubernetes.io/instance-type")).toString();
    if (node.instanceType.isEmpty()) {
        node.instanceType = labels
                                .value(QStringLiteral(
                                    "beta.kubernetes.io/instance-type"))
                                .toString();
    }
    node.zone =
        labels.value(QStringLiteral("topology.kubernetes.io/zone")).toString();
    if (node.zone.isEmpty()) {
        node.zone = labels
                        .value(QStringLiteral(
                            "failure-domain.beta.kubernetes.io/zone"))
                        .toString();
    }
    node.region =
        labels.value(QStringLiteral("topology.kubernetes.io/region")).toString();

    QStringList roles;
    const QString rolePrefix = QStringLiteral("node-role.kubernetes.io/");
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it) {
        if (it.key().startsWith(rolePrefix)) {
            const QString role = it.key().mid(rolePrefix.size());
            if (!role.isEmpty()) {
                roles.append(role);
            }
        }
    }
    node.roles = roles.join(QLatin1Char(','));

    node.providerId =
        item.value(QStringLiteral("spec"))
            .toObject()
            .value(QStringLiteral("providerID"))
            .toString();

    const QJsonObject status = item.value(QStringLiteral("status")).toObject();
    const QJsonObject nodeInfo = status.value(QStringLiteral("nodeInfo")).toObject();
    node.kubeletVersion =
        nodeInfo.value(QStringLiteral("kubeletVersion")).toString();
    node.runtime =
        nodeInfo.value(QStringLiteral("containerRuntimeVersion")).toString();
    node.osImage = nodeInfo.value(QStringLiteral("osImage")).toString();

    const QJsonObject capacity =
        status.value(QStringLiteral("capacity")).toObject();
    const QJsonObject allocatable =
        status.value(QStringLiteral("allocatable")).toObject();
    node.cpuCapacityCores =
        kubeParseCpuCores(capacity.value(QStringLiteral("cpu")).toString());
    node.cpuAllocatableCores =
        kubeParseCpuCores(allocatable.value(QStringLiteral("cpu")).toString());
    node.memCapacityBytes = static_cast<unsigned long long>(
        kubeParseMemoryBytes(capacity.value(QStringLiteral("memory")).toString()));
    node.memAllocatableBytes = static_cast<unsigned long long>(
        kubeParseMemoryBytes(allocatable.value(QStringLiteral("memory")).toString()));
    node.podsCapacity =
        capacity.value(QStringLiteral("pods")).toString().toInt();
    node.podsAllocatable =
        allocatable.value(QStringLiteral("pods")).toString().toInt();

    const QJsonArray conditions =
        status.value(QStringLiteral("conditions")).toArray();
    for (const QJsonValue &value : conditions) {
        const QJsonObject condition = value.toObject();
        if (condition.value(QStringLiteral("type")).toString()
                == QStringLiteral("Ready")
            && condition.value(QStringLiteral("status")).toString()
                   == QStringLiteral("True")) {
            node.ready = true;
        }
    }
    return node;
}

KubePod parsePod(const QJsonObject &item) {
    const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    const QJsonObject spec = item.value(QStringLiteral("spec")).toObject();
    const QJsonObject status = item.value(QStringLiteral("status")).toObject();

    KubePod pod;
    pod.ns = metadata.value(QStringLiteral("namespace")).toString();
    pod.name = metadata.value(QStringLiteral("name")).toString();
    pod.phase = status.value(QStringLiteral("phase")).toString();
    pod.nodeName = spec.value(QStringLiteral("nodeName")).toString();

    const QJsonArray containers = spec.value(QStringLiteral("containers")).toArray();
    for (const QJsonValue &containerValue : containers) {
        const QJsonObject resources =
            containerValue.toObject().value(QStringLiteral("resources")).toObject();
        const QJsonObject requests =
            resources.value(QStringLiteral("requests")).toObject();
        const QJsonObject limits =
            resources.value(QStringLiteral("limits")).toObject();
        const QString cpuRequest = requests.value(QStringLiteral("cpu")).toString();
        const QString memRequest =
            requests.value(QStringLiteral("memory")).toString();
        pod.cpuRequestCores += kubeParseCpuCores(cpuRequest);
        pod.memRequestBytes += kubeParseMemoryBytes(memRequest);
        pod.cpuLimitCores +=
            kubeParseCpuCores(limits.value(QStringLiteral("cpu")).toString());
        pod.memLimitBytes +=
            kubeParseMemoryBytes(limits.value(QStringLiteral("memory")).toString());
        if (cpuRequest.isEmpty()) {
            ++pod.containersWithoutCpuRequest;
        }
        if (memRequest.isEmpty()) {
            ++pod.containersWithoutMemRequest;
        }
    }

    const QJsonArray containerStatuses =
        status.value(QStringLiteral("containerStatuses")).toArray();
    for (const QJsonValue &csValue : containerStatuses) {
        if (!csValue.isObject()) {
            continue;
        }
        const QJsonObject cs = csValue.toObject();
        const QJsonObject state = cs.value(QStringLiteral("state")).toObject();

        const int restarts = cs.value(QStringLiteral("restartCount")).toInt(0);
        if (restarts > pod.restarts) {
            pod.restarts = restarts;
        }

        const QString waitingReason =
            state.value(QStringLiteral("waiting"))
                .toObject()
                .value(QStringLiteral("reason"))
                .toString();
        const QString terminatedReason =
            state.value(QStringLiteral("terminated"))
                .toObject()
                .value(QStringLiteral("reason"))
                .toString();
        const QString reason =
            !waitingReason.isEmpty() ? waitingReason : terminatedReason;
        if (!reason.isEmpty()) {
            pod.reason = reason;
        }

        const QString lastTerminatedReason =
            cs.value(QStringLiteral("lastState"))
                .toObject()
                .value(QStringLiteral("terminated"))
                .toObject()
                .value(QStringLiteral("reason"))
                .toString();
        if (!lastTerminatedReason.isEmpty()) {
            pod.lastReason = lastTerminatedReason;
        }
    }

    return pod;
}

KubeEvent parseEvent(const QJsonObject &item) {
    const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    const QJsonObject involved =
        item.value(QStringLiteral("involvedObject")).toObject();

    KubeEvent event;
    event.ns = metadata.value(QStringLiteral("namespace")).toString();
    event.involvedName = involved.value(QStringLiteral("name")).toString();
    event.type = item.value(QStringLiteral("type")).toString();
    event.reason = item.value(QStringLiteral("reason")).toString();
    event.message = item.value(QStringLiteral("message")).toString();
    if (event.involvedName.isEmpty()) {
        event.involvedName = metadata.value(QStringLiteral("name")).toString();
    }
    event.count = item.value(QStringLiteral("count")).toInt(1);

    // Prefer eventTime, fall back to lastTimestamp. Both are RFC3339 strings;
    // Kubernetes may include sub-millisecond precision, so try both parsers.
    const QString eventTime =
        item.value(QStringLiteral("eventTime")).toString();
    const QString lastTimestamp =
        item.value(QStringLiteral("lastTimestamp")).toString();
    const QString stamp = !eventTime.isEmpty() ? eventTime : lastTimestamp;
    QDateTime parsed = QDateTime::fromString(stamp, Qt::ISODateWithMs);
    if (!parsed.isValid()) {
        parsed = QDateTime::fromString(stamp, Qt::ISODate);
    }
    event.lastTimestampMs = parsed.isValid() ? parsed.toMSecsSinceEpoch() : 0;

    return event;
}

KubeDeployment parseDeployment(const QJsonObject &item) {
    const QJsonObject metadata = item.value(QStringLiteral("metadata")).toObject();
    const QJsonObject spec = item.value(QStringLiteral("spec")).toObject();
    const QJsonObject status = item.value(QStringLiteral("status")).toObject();

    KubeDeployment deployment;
    deployment.ns = metadata.value(QStringLiteral("namespace")).toString();
    deployment.name = metadata.value(QStringLiteral("name")).toString();
    deployment.generation = metadata.value(QStringLiteral("generation")).toInt(0);
    deployment.desired = spec.value(QStringLiteral("replicas")).toInt(1);
    deployment.available =
        status.value(QStringLiteral("availableReplicas")).toInt(0);
    deployment.unavailable =
        status.value(QStringLiteral("unavailableReplicas")).toInt(0);
    return deployment;
}

} // namespace

KubeResources parseKubectlResources(const QByteArray &json, bool *ok) {
    if (ok != nullptr) {
        *ok = true;
    }

    KubeResources resources;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return resources;
    }

    const QJsonValue itemsValue = doc.object().value(QStringLiteral("items"));
    if (!itemsValue.isArray()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return resources;
    }

    const QJsonArray items = itemsValue.toArray();
    for (const QJsonValue &itemValue : items) {
        if (!itemValue.isObject()) {
            continue;
        }

        const QJsonObject item = itemValue.toObject();
        const QString kind = item.value(QStringLiteral("kind")).toString();

        const QJsonObject metadata =
            item.value(QStringLiteral("metadata")).toObject();
        ObjectRef ref;
        ref.apiVersion = item.value(QStringLiteral("apiVersion")).toString();
        ref.kind = kind;
        ref.ns = metadata.value(QStringLiteral("namespace")).toString();
        ref.name = metadata.value(QStringLiteral("name")).toString();
        resources.objects.append(ref);

        if (kind == QStringLiteral("Deployment")) {
            resources.deployments.append(parseDeployment(item));
        } else if (kind == QStringLiteral("Event")) {
            resources.events.append(parseEvent(item));
        } else if (kind == QStringLiteral("Pod")) {
            resources.pods.append(parsePod(item));
        } else if (kind == QStringLiteral("Node")) {
            resources.nodes.append(parseNode(item));
        }
    }

    return resources;
}

bool kubePodIsIssue(const KubePod &pod) {
    if (pod.phase == QStringLiteral("Failed")
        || pod.phase == QStringLiteral("Unknown")) {
        return true;
    }
    if (!badReason(pod).isEmpty()) {
        return true;
    }
    return pod.restarts >= 5;
}

bool kubeDeploymentIsIssue(const KubeDeployment &deployment) {
    if (deployment.unavailable > 0) {
        return true;
    }
    return deployment.desired > 0 && deployment.available < deployment.desired;
}

int kubeIssueCount(const QList<KubePod> &pods,
                   const QList<KubeDeployment> &deployments) {
    int issues = 0;
    for (const KubePod &pod : pods) {
        if (kubePodIsIssue(pod)) {
            ++issues;
        }
    }
    for (const KubeDeployment &deployment : deployments) {
        if (kubeDeploymentIsIssue(deployment)) {
            ++issues;
        }
    }
    return issues;
}

KubeState classifyKubeFailure(const QByteArray &payload) {
    const QByteArray lower = payload.toLower();

    if (lower.contains("no configuration")
        || lower.contains("connection refused")
        || lower.contains("was refused")
        || lower.contains("connection to the server")
        || lower.contains("no route to host")
        || lower.contains("unable to connect")
        || lower.contains("couldn't get current server")
        || lower.contains("connection reset")
        || lower.contains("did you specify the right host")) {
        return KubeState::Unreachable;
    }

    return KubeState::CommandFailed;
}

QString kubeSummaryText(const KubeSnapshot &snapshot) {
    switch (snapshot.state) {
    case KubeState::NotConfigured:
        // M58: silent-empty card is an honesty violation; say what is
        // needed (a kubeconfig + kubectl, or an in-cluster service
        // account).
        return QStringLiteral(
            "k8s: not configured - no kubecontext (kubeconfig or cluster "
            "credentials needed)");
    case KubeState::Unreachable:
        return QStringLiteral("k8s: unreachable");
    case KubeState::TimedOut:
        return QStringLiteral("k8s: timed out");
    case KubeState::CommandFailed:
        return QStringLiteral("k8s: command failed");
    case KubeState::ParseFailed:
        return QStringLiteral("k8s: parse failed");
    case KubeState::Stale:
        break;
    case KubeState::Ok:
    default:
        break;
    }

    if (snapshot.issueCount > 0) {
        return QStringLiteral("k8s: %1 %2")
            .arg(snapshot.issueCount)
            .arg(snapshot.issueCount == 1 ? QStringLiteral("issue")
                                          : QStringLiteral("issues"));
    }

    QString text = QStringLiteral("k8s: %1 pods").arg(snapshot.totalPods);
    if (snapshot.state == KubeState::Stale || snapshot.stale) {
        text += QStringLiteral(" | stale");
    }
    return text;
}

QString kubeDetailText(const KubeSnapshot &snapshot) {
    QStringList lines;

    for (const KubePod &pod : snapshot.pods) {
        if (!kubePodIsIssue(pod)) {
            continue;
        }

        QString detail = QStringLiteral("- pod %1/%2").arg(pod.ns, pod.name);
        if (!pod.phase.isEmpty()) {
            detail += QStringLiteral(" | %1").arg(pod.phase);
        }
        if (!pod.reason.isEmpty()) {
            detail += QStringLiteral(" | %1").arg(pod.reason);
        }
        if (pod.restarts > 0) {
            detail += QStringLiteral(" | r=%1").arg(pod.restarts);
        }
        lines.append(detail);
    }

    for (const KubeDeployment &deployment : snapshot.deployments) {
        if (!kubeDeploymentIsIssue(deployment)) {
            continue;
        }

        lines.append(QStringLiteral("- deploy %1/%2 | %3/%4 ready")
                         .arg(deployment.ns, deployment.name)
                         .arg(deployment.available)
                         .arg(deployment.desired));
    }

    if (lines.isEmpty()) {
        return QStringLiteral("no issues");
    }

    return lines.join(QLatin1Char('\n'));
}

bool kubeEventIsWarning(const KubeEvent &event) {
    return event.type == QStringLiteral("Warning");
}

int kubeWarningCount(const QList<KubeEvent> &events) {
    int count = 0;
    for (const KubeEvent &event : events) {
        if (kubeEventIsWarning(event)) {
            ++count;
        }
    }
    return count;
}

QString kubePodsText(const KubeSnapshot &snapshot) {
    QStringList lines;
    for (const KubePod &pod : snapshot.pods) {
        QString line = QStringLiteral("%1 %2/%3  %4")
                           .arg(kubePodIsIssue(pod) ? QStringLiteral("!")
                                                    : QStringLiteral(" "),
                                pod.ns, pod.name, pod.phase);
        if (!pod.reason.isEmpty()) {
            line += QStringLiteral("  %1").arg(pod.reason);
        }
        if (!pod.lastReason.isEmpty()) {
            line += QStringLiteral("  last:%1").arg(pod.lastReason);
        }
        if (pod.restarts > 0) {
            line += QStringLiteral("  r=%1").arg(pod.restarts);
        }
        lines.append(line);
        if (lines.size() >= 60) {
            lines.append(QStringLiteral("..."));
            break;
        }
    }
    return lines.isEmpty() ? QStringLiteral("no pods") : lines.join('\n');
}

QString kubeDeploymentsText(const KubeSnapshot &snapshot) {
    QStringList lines;
    for (const KubeDeployment &deployment : snapshot.deployments) {
        lines.append(QStringLiteral("%1 %2/%3  %4/%5 ready")
                         .arg(kubeDeploymentIsIssue(deployment)
                                  ? QStringLiteral("!")
                                  : QStringLiteral(" "),
                              deployment.ns, deployment.name)
                         .arg(deployment.available)
                         .arg(deployment.desired));
        if (lines.size() >= 40) {
            lines.append(QStringLiteral("..."));
            break;
        }
    }
    return lines.isEmpty() ? QStringLiteral("no deployments")
                           : lines.join('\n');
}

QString kubeEventsText(const KubeSnapshot &snapshot) {
    QList<KubeEvent> warnings;
    for (const KubeEvent &event : snapshot.events) {
        if (kubeEventIsWarning(event)) {
            warnings.append(event);
        }
    }
    std::sort(warnings.begin(), warnings.end(),
              [](const KubeEvent &a, const KubeEvent &b) {
                  return a.lastTimestampMs > b.lastTimestampMs;
              });

    QStringList lines;
    for (const KubeEvent &event : warnings) {
        lines.append(QStringLiteral("%1  %2/%3  x%4  %5")
                         .arg(event.reason.isEmpty() ? QStringLiteral("Warning")
                                                     : event.reason,
                              event.ns, event.involvedName)
                         .arg(event.count)
                         .arg(event.message));
        if (lines.size() >= 40) {
            lines.append(QStringLiteral("..."));
            break;
        }
    }
    return lines.isEmpty() ? QStringLiteral("no warning events")
                           : lines.join('\n');
}

QList<KubeDiagnosis> kubeDiagnose(const QList<KubePod> &pods,
                                  const QList<KubeEvent> &events) {
    QList<KubeDiagnosis> out;

    for (const KubePod &pod : pods) {
        if (!kubePodIsIssue(pod) && pod.reason.isEmpty()) {
            continue;
        }

        QString eventMessage;
        for (const KubeEvent &event : events) {
            if (event.ns == pod.ns && event.involvedName == pod.name
                && kubeEventIsWarning(event)) {
                eventMessage = event.message;
            }
        }

        const QString reason =
            !pod.reason.isEmpty() ? pod.reason : pod.lastReason;

        KubeDiagnosis diag;
        diag.ns = pod.ns;
        diag.pod = pod.name;

        if (reason == QStringLiteral("ImagePullBackOff")
            || reason == QStringLiteral("ErrImagePull")
            || reason == QStringLiteral("InvalidImageName")) {
            diag.severity = QStringLiteral("critical");
            diag.title = QStringLiteral("image pull failing");
            diag.detail = eventMessage.isEmpty() ? reason : eventMessage;
        } else if (reason == QStringLiteral("CrashLoopBackOff")) {
            diag.severity = QStringLiteral("critical");
            diag.title = QStringLiteral("crash loop");
            diag.detail =
                pod.lastReason == QStringLiteral("OOMKilled")
                    ? QStringLiteral("container OOM-killed; raise its memory limit")
                    : QStringLiteral("container exits repeatedly (r=%1)")
                          .arg(pod.restarts);
        } else if (reason == QStringLiteral("OOMKilled")) {
            diag.severity = QStringLiteral("critical");
            diag.title = QStringLiteral("OOMKilled");
            diag.detail = QStringLiteral("container exceeded its memory limit");
        } else if (reason == QStringLiteral("CreateContainerConfigError")
                   || reason == QStringLiteral("CreateContainerError")) {
            diag.severity = QStringLiteral("critical");
            diag.title = QStringLiteral("container config error");
            diag.detail =
                eventMessage.isEmpty()
                    ? QStringLiteral("missing ConfigMap/Secret?")
                    : eventMessage;
        } else if (reason == QStringLiteral("RunContainerError")) {
            diag.severity = QStringLiteral("critical");
            diag.title = QStringLiteral("container failed to start");
            diag.detail = eventMessage;
        } else if (pod.phase == QStringLiteral("Pending")) {
            diag.severity = QStringLiteral("warning");
            diag.title = QStringLiteral("pending / unschedulable");
            diag.detail = eventMessage.isEmpty()
                              ? QStringLiteral("not scheduled")
                              : eventMessage;
        } else if (pod.phase == QStringLiteral("Failed")) {
            diag.severity = QStringLiteral("critical");
            diag.title = QStringLiteral("pod failed");
            diag.detail = reason;
        } else if (pod.restarts >= 5) {
            diag.severity = QStringLiteral("warning");
            diag.title = QStringLiteral("frequent restarts");
            diag.detail = QStringLiteral("r=%1").arg(pod.restarts);
        } else {
            continue;
        }

        out.append(diag);
    }

    return out;
}

QList<KubeChange> kubeDiff(const KubeResources &prev, const KubeResources &cur,
                           qint64 nowMs) {
    QList<KubeChange> changes;

    auto podKey = [](const KubePod &pod) {
        return pod.ns + QLatin1Char('/') + pod.name;
    };

    QHash<QString, KubePod> prevPods;
    for (const KubePod &pod : prev.pods) {
        prevPods.insert(podKey(pod), pod);
    }
    QSet<QString> curPodKeys;
    for (const KubePod &pod : cur.pods) {
        const QString key = podKey(pod);
        curPodKeys.insert(key);
        if (!prevPods.contains(key)) {
            changes.append({nowMs, pod.ns, pod.name, QStringLiteral("Pod"),
                            QStringLiteral("created")});
            continue;
        }
        const KubePod before = prevPods.value(key);
        if (pod.phase != before.phase) {
            changes.append({nowMs, pod.ns, pod.name, QStringLiteral("Pod"),
                            QStringLiteral("phase %1 -> %2")
                                .arg(before.phase, pod.phase)});
        }
        if (pod.reason != before.reason && !pod.reason.isEmpty()) {
            changes.append({nowMs, pod.ns, pod.name, QStringLiteral("Pod"),
                            QStringLiteral("reason %1").arg(pod.reason)});
        }
        if (pod.restarts > before.restarts) {
            changes.append({nowMs, pod.ns, pod.name, QStringLiteral("Pod"),
                            QStringLiteral("restarted (r=%1)").arg(pod.restarts)});
        }
    }
    for (const KubePod &pod : prev.pods) {
        if (!curPodKeys.contains(podKey(pod))) {
            changes.append({nowMs, pod.ns, pod.name, QStringLiteral("Pod"),
                            QStringLiteral("deleted")});
        }
    }

    QHash<QString, KubeDeployment> prevDeploys;
    for (const KubeDeployment &dep : prev.deployments) {
        prevDeploys.insert(dep.ns + QLatin1Char('/') + dep.name, dep);
    }
    QSet<QString> curDeployKeys;
    for (const KubeDeployment &dep : cur.deployments) {
        const QString key = dep.ns + QLatin1Char('/') + dep.name;
        curDeployKeys.insert(key);
        if (!prevDeploys.contains(key)) {
            changes.append({nowMs, dep.ns, dep.name,
                            QStringLiteral("Deployment"), QStringLiteral("created")});
            continue;
        }
        const KubeDeployment before = prevDeploys.value(key);
        if (dep.generation != 0 && dep.generation != before.generation) {
            changes.append({nowMs, dep.ns, dep.name, QStringLiteral("Deployment"),
                            QStringLiteral("rollout (gen %1)").arg(dep.generation)});
        }
        if (dep.available != before.available) {
            changes.append({nowMs, dep.ns, dep.name, QStringLiteral("Deployment"),
                            QStringLiteral("ready %1/%2")
                                .arg(dep.available)
                                .arg(dep.desired)});
        }
    }
    for (const KubeDeployment &dep : prev.deployments) {
        if (!curDeployKeys.contains(dep.ns + QLatin1Char('/') + dep.name)) {
            changes.append({nowMs, dep.ns, dep.name,
                            QStringLiteral("Deployment"), QStringLiteral("deleted")});
        }
    }

    return changes;
}

QString kubeDiagnosisText(const QList<KubeDiagnosis> &diagnoses) {
    if (diagnoses.isEmpty()) {
        return QStringLiteral("no problems detected");
    }
    QStringList lines;
    for (const KubeDiagnosis &diag : diagnoses) {
        lines.append(QStringLiteral("[%1] %2/%3  %4 - %5")
                         .arg(diag.severity.toUpper(), diag.ns, diag.pod,
                              diag.title, diag.detail));
        if (lines.size() >= 40) {
            lines.append(QStringLiteral("..."));
            break;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString kubeTimelineText(const QList<KubeChange> &changes) {
    if (changes.isEmpty()) {
        return QStringLiteral("no changes");
    }
    QStringList lines;
    for (const KubeChange &change : changes) {
        const QString time =
            QDateTime::fromMSecsSinceEpoch(change.atMs)
                .toString(QStringLiteral("HH:mm:ss"));
        lines.append(QStringLiteral("%1  %2 %3/%4: %5")
                         .arg(time, change.kind, change.ns, change.object,
                              change.summary));
        if (lines.size() >= 40) {
            lines.append(QStringLiteral("..."));
            break;
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString kubeNodesText(const QList<KubeNode> &nodes) {
    if (nodes.isEmpty()) {
        return QStringLiteral("no nodes");
    }
    QString text;
    for (const KubeNode &node : nodes) {
        const double memCapacity = node.memCapacityBytes / (1024.0 * 1024.0 * 1024.0);
        const double memAlloc = node.memAllocatableBytes / (1024.0 * 1024.0 * 1024.0);
        text += QStringLiteral("%1 %2  %3  k8s %4  cpu %5/%6  mem %7/%8GiB  "
                               "pods %9/%10%11%12\n")
                    .arg(node.name.leftJustified(20, ' '),
                         node.ready ? QStringLiteral("Ready ")
                                    : QStringLiteral("NotRdy"),
                         node.roles.leftJustified(10, ' '),
                         node.kubeletVersion,
                         QString::number(node.cpuAllocatableCores, 'f', 1),
                         QString::number(node.cpuCapacityCores, 'f', 1),
                         QString::number(memAlloc, 'f', 1),
                         QString::number(memCapacity, 'f', 1))
                    .arg(node.podsRunning)
                    .arg(node.podsAllocatable)
                    .arg(node.instanceType.isEmpty()
                             ? QString()
                             : QStringLiteral("  ") + node.instanceType,
                         node.zone.isEmpty()
                             ? QString()
                             : QStringLiteral("  ") + node.zone);
    }
    return text.trimmed();
}
