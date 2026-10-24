#include "CostModel.h"
#include "KubeStatus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

KubeSnapshot snap(const QList<KubePod> &pods,
                  const QList<KubeDeployment> &deployments) {
    KubeSnapshot s;
    s.state = KubeState::Ok;
    s.pods = pods;
    s.deployments = deployments;
    s.totalPods = pods.size();
    s.issueCount = kubeIssueCount(pods, deployments);
    return s;
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&failures](bool condition, const char *message) {
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition) {
            ++failures;
        }
    };

    const QByteArray json = QByteArrayLiteral(
        "{\"items\":["
        "{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"default\",\"name\":\"web-1\"},"
        "\"status\":{\"phase\":\"Running\",\"containerStatuses\":["
        "{\"restartCount\":0,\"state\":{\"running\":{}}}]}},"
        "{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"default\",\"name\":\"crash-1\"},"
        "\"status\":{\"phase\":\"Running\",\"containerStatuses\":["
        "{\"restartCount\":7,\"state\":{\"waiting\":{\"reason\":\"CrashLoopBackOff\"}}}]}},"
        "{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"kube-system\",\"name\":\"failed-1\"},"
        "\"status\":{\"phase\":\"Failed\",\"containerStatuses\":[]}},"
        "{\"kind\":\"Deployment\",\"metadata\":{\"namespace\":\"default\",\"name\":\"nginx\"},"
        "\"spec\":{\"replicas\":3},\"status\":{\"availableReplicas\":1,\"unavailableReplicas\":2}},"
        "{\"kind\":\"Deployment\",\"metadata\":{\"namespace\":\"default\",\"name\":\"ok-dep\"},"
        "\"spec\":{\"replicas\":2},\"status\":{\"availableReplicas\":2}}"
        "]}");

    bool ok = false;
    const KubeResources res = parseKubectlResources(json, &ok);
    check(ok, "valid kubectl json parses");
    check(res.pods.size() == 3, "parsed three pods");
    check(res.deployments.size() == 2, "parsed two deployments");
    check(res.pods.at(1).reason == QStringLiteral("CrashLoopBackOff"),
          "parsed pod waiting reason");
    check(res.pods.at(1).restarts == 7, "parsed pod restart count");

    const KubeDeployment &nginx = res.deployments.at(0);
    check(nginx.name == QStringLiteral("nginx"), "parsed deployment name");
    check(nginx.desired == 3 && nginx.available == 1 && nginx.unavailable == 2,
          "parsed deployment replica counts");
    check(kubeDeploymentIsIssue(nginx), "deployment with unavailable replicas is an issue");
    check(!kubeDeploymentIsIssue(res.deployments.at(1)),
          "deployment fully available is not an issue");

    check(kubeIssueCount(res.pods, res.deployments) == 3,
          "issue count = 2 pods + 1 deployment");

    bool emptyOk = false;
    const KubeResources empty = parseKubectlResources(
        QByteArrayLiteral("{\"items\":[]}"), &emptyOk);
    check(emptyOk && empty.pods.isEmpty() && empty.deployments.isEmpty(),
          "empty items -> nothing");

    bool badOk = true;
    parseKubectlResources(QByteArrayLiteral("not json"), &badOk);
    check(!badOk, "malformed json -> parse failed");

    check(classifyKubeFailure(
              QByteArrayLiteral("The connection to the server was refused"))
              == KubeState::Unreachable,
          "connection refused -> unreachable");
    check(classifyKubeFailure(QByteArrayLiteral("boom"))
              == KubeState::CommandFailed,
          "other error -> command failed");

    const KubeSnapshot summary = snap(res.pods, res.deployments);
    check(kubeSummaryText(summary) == QStringLiteral("k8s: 3 issues"),
          "summary counts pod+deployment issues");
    check(kubeSummaryText(KubeSnapshot{})
              == QStringLiteral("k8s: not configured - no kubecontext "
                                "(kubeconfig or cluster credentials needed)"),
          "NotConfigured -> explicit hint (M58, was silent-empty)");

    KubeSnapshot noIssues;
    noIssues.state = KubeState::Ok;
    noIssues.totalPods = 4;
    check(kubeSummaryText(noIssues) == QStringLiteral("k8s: 4 pods"),
          "summary healthy pod count");

    const QString detail = kubeDetailText(summary);
    check(detail.contains(QStringLiteral("crash-1")), "detail lists crash pod");
    check(detail.contains(QStringLiteral("deploy default/nginx")),
          "detail lists unhealthy deployment");
    check(!detail.contains(QStringLiteral("web-1")), "detail omits healthy pod");
    check(!detail.contains(QStringLiteral("ok-dep")),
          "detail omits healthy deployment");

    const QByteArray oomJson = QByteArrayLiteral(
        "{\"items\":[{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"d\",\"name\":\"oom-1\"},"
        "\"status\":{\"phase\":\"Running\",\"containerStatuses\":["
        "{\"restartCount\":0,\"state\":{\"terminated\":{\"reason\":\"OOMKilled\"}}}]}}]}");
    bool oomOk = false;
    const KubeResources oom = parseKubectlResources(oomJson, &oomOk);
    check(oomOk && oom.pods.size() == 1
              && oom.pods.at(0).reason == QStringLiteral("OOMKilled"),
          "parsed terminated reason (OOMKilled)");
    check(kubePodIsIssue(oom.pods.at(0)), "OOMKilled is an issue");

    const QByteArray eventsJson = QByteArrayLiteral(
        "{\"items\":["
        "{\"kind\":\"Event\",\"metadata\":{\"namespace\":\"default\",\"name\":\"web.1\"},"
        "\"involvedObject\":{\"name\":\"web-1\"},\"type\":\"Warning\",\"reason\":\"BackOff\","
        "\"message\":\"Back-off restarting failed container\",\"count\":4,"
        "\"lastTimestamp\":\"2026-10-13T22:00:00Z\"},"
        "{\"kind\":\"Event\",\"metadata\":{\"namespace\":\"default\",\"name\":\"ok.1\"},"
        "\"involvedObject\":{\"name\":\"web-1\"},\"type\":\"Normal\",\"reason\":\"Scheduled\","
        "\"message\":\"Assigned to node\",\"count\":1}]}");
    bool evOk = false;
    const KubeResources ev = parseKubectlResources(eventsJson, &evOk);
    check(evOk && ev.events.size() == 2, "parsed two events");
    check(ev.events.at(0).type == QStringLiteral("Warning")
              && ev.events.at(0).reason == QStringLiteral("BackOff")
              && ev.events.at(0).involvedName == QStringLiteral("web-1")
              && ev.events.at(0).count == 4
              && ev.events.at(0).lastTimestampMs > 0,
          "parsed warning event fields");
    check(kubeWarningCount(ev.events) == 1, "warning event count = 1");

    KubeSnapshot evSnap;
    evSnap.state = KubeState::Ok;
    evSnap.events = ev.events;
    check(kubeEventsText(evSnap).contains(QStringLiteral("Back-off restarting")),
          "events text lists warning");
    check(!kubeEventsText(evSnap).contains(QStringLiteral("Assigned to node")),
          "events text omits normal events");

    const QByteArray lastStateJson = QByteArrayLiteral(
        "{\"items\":[{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"d\",\"name\":\"restart-1\"},"
        "\"status\":{\"phase\":\"Running\",\"containerStatuses\":["
        "{\"restartCount\":3,\"state\":{\"running\":{}},"
        "\"lastState\":{\"terminated\":{\"reason\":\"OOMKilled\"}}}]}}]}");
    bool lsOk = false;
    const KubeResources ls = parseKubectlResources(lastStateJson, &lsOk);
    check(lsOk && ls.pods.size() == 1
              && ls.pods.at(0).lastReason == QStringLiteral("OOMKilled"),
          "parsed pod lastState terminated reason");

    // ---- diagnosis ----
    {
        KubePod crash;
        crash.ns = QStringLiteral("default");
        crash.name = QStringLiteral("crash-1");
        crash.phase = QStringLiteral("Running");
        crash.reason = QStringLiteral("CrashLoopBackOff");
        crash.lastReason = QStringLiteral("OOMKilled");
        crash.restarts = 7;
        KubePod pull;
        pull.ns = QStringLiteral("default");
        pull.name = QStringLiteral("pull-1");
        pull.phase = QStringLiteral("Pending");
        pull.reason = QStringLiteral("ImagePullBackOff");
        KubePod healthy;
        healthy.ns = QStringLiteral("default");
        healthy.name = QStringLiteral("ok-1");
        healthy.phase = QStringLiteral("Running");

        KubeEvent ev;
        ev.ns = QStringLiteral("default");
        ev.involvedName = QStringLiteral("pull-1");
        ev.type = QStringLiteral("Warning");
        ev.reason = QStringLiteral("Failed");
        ev.message = QStringLiteral("Failed to pull image: not found");
        ev.count = 3;

        const QList<KubeDiagnosis> diags =
            kubeDiagnose({crash, pull, healthy}, {ev});
        check(diags.size() == 2, "diagnose: two problems");
        check(diags.at(0).title == QStringLiteral("crash loop")
                  && diags.at(0).severity == QStringLiteral("critical")
                  && diags.at(0).detail.contains(QStringLiteral("memory")),
              "diagnose: crash loop -> OOM guidance");
        check(diags.at(1).title == QStringLiteral("image pull failing")
                  && diags.at(1).detail.contains(QStringLiteral("Failed to pull")),
              "diagnose: image pull uses event message");
        check(kubeDiagnosisText(diags).contains(QStringLiteral("crash-1")),
              "diagnosis text lists pod");
        check(kubeDiagnose({healthy}, {}).isEmpty(),
              "diagnose: healthy pod -> none");
    }

    // ---- change timeline ----
    {
        KubeResources prev;
        KubePod pod;
        pod.ns = QStringLiteral("default");
        pod.name = QStringLiteral("web-1");
        pod.phase = QStringLiteral("Running");
        prev.pods = {pod};
        KubeDeployment dep;
        dep.ns = QStringLiteral("default");
        dep.name = QStringLiteral("nginx");
        dep.desired = 3;
        dep.available = 3;
        dep.generation = 1;
        prev.deployments = {dep};

        KubeResources cur = prev;
        cur.pods[0].restarts = 2;
        KubePod created;
        created.ns = QStringLiteral("default");
        created.name = QStringLiteral("new-1");
        created.phase = QStringLiteral("Pending");
        cur.pods.append(created);
        cur.deployments[0].generation = 2;
        cur.deployments[0].available = 1;

        const QList<KubeChange> changes = kubeDiff(prev, cur, 1000);
        bool sawRestart = false;
        bool sawCreated = false;
        bool sawRollout = false;
        bool sawReady = false;
        for (const KubeChange &change : changes) {
            if (change.object == QStringLiteral("web-1")
                && change.summary.contains(QStringLiteral("restarted"))) {
                sawRestart = true;
            }
            if (change.object == QStringLiteral("new-1")
                && change.summary == QStringLiteral("created")) {
                sawCreated = true;
            }
            if (change.object == QStringLiteral("nginx")
                && change.summary.contains(QStringLiteral("rollout"))) {
                sawRollout = true;
            }
            if (change.object == QStringLiteral("nginx")
                && change.summary.contains(QStringLiteral("ready 1/3"))) {
                sawReady = true;
            }
        }
        check(sawRestart && sawCreated && sawRollout && sawReady,
              "diff: restart, create, rollout, readiness captured");
        check(kubeTimelineText(changes).contains(QStringLiteral("web-1")),
              "timeline text lists change");
    }

    {
        auto approxEq = [](double a, double b) {
            const double scale = std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
            return std::fabs(a - b) <= 1e-6 * scale;
        };

        check(approxEq(kubeParseCpuCores(QStringLiteral("100m")), 0.1),
              "cpu: 100m -> 0.1 cores");
        check(approxEq(kubeParseCpuCores(QStringLiteral("1500m")), 1.5),
              "cpu: 1500m -> 1.5 cores");
        check(approxEq(kubeParseCpuCores(QStringLiteral("2")), 2.0),
              "cpu: 2 -> 2 cores");
        check(approxEq(kubeParseCpuCores(QString()), 0.0),
              "cpu: empty -> 0");

        check(approxEq(kubeParseMemoryBytes(QStringLiteral("256Mi")),
                       268435456.0),
              "mem: 256Mi -> bytes");
        check(approxEq(kubeParseMemoryBytes(QStringLiteral("1Gi")), 1073741824.0),
              "mem: 1Gi -> bytes");
        check(approxEq(kubeParseMemoryBytes(QStringLiteral("500M")), 5e8),
              "mem: 500M -> bytes (decimal)");
        check(approxEq(kubeParseMemoryBytes(QStringLiteral("128974848")),
                       128974848.0),
              "mem: plain bytes");

        const QByteArray costJson = QByteArrayLiteral(
            "{\"items\":["
            "{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"prod\",\"name\":\"api-1\"},"
            "\"spec\":{\"containers\":["
            "{\"resources\":{\"requests\":{\"cpu\":\"500m\",\"memory\":\"512Mi\"},"
            "\"limits\":{\"cpu\":\"1\",\"memory\":\"1Gi\"}}},"
            "{\"resources\":{\"requests\":{\"memory\":\"256Mi\"}}}]},"
            "\"status\":{\"phase\":\"Running\"}},"
            "{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"dev\",\"name\":\"job-1\"},"
            "\"spec\":{\"containers\":["
            "{\"resources\":{\"requests\":{\"cpu\":\"1000m\",\"memory\":\"2Gi\"}}}]},"
            "\"status\":{\"phase\":\"Running\"}}"
            "]}");

        bool costOk = false;
        const KubeResources costRes = parseKubectlResources(costJson, &costOk);
        check(costOk && costRes.pods.size() == 2, "cost json parses two pods");

        const KubePod &api = costRes.pods.at(0);
        check(approxEq(api.cpuRequestCores, 0.5), "pod: summed cpu request");
        check(approxEq(api.memRequestBytes, 805306368.0), "pod: summed mem request");
        check(approxEq(api.cpuLimitCores, 1.0), "pod: summed cpu limit");
        check(approxEq(api.memLimitBytes, 1073741824.0), "pod: summed mem limit");
        check(api.containersWithoutCpuRequest == 1,
              "pod: counted container without cpu request");
        check(api.containersWithoutMemRequest == 0,
              "pod: counted container with mem request");

        const double cpuPrice = 0.03;
        const double memPrice = 0.004;
        const CostSummary cost =
            computeClusterCost(costRes.pods, cpuPrice, memPrice);
        // prod: (0.5*0.03 + 0.75*0.004)*730 = 13.14
        // dev:  (1.0*0.03 + 2.0*0.004)*730  = 27.74
        check(approxEq(cost.totalMonthly, 40.88), "cost: total monthly");
        check(cost.podsPriced == 2, "cost: priced two pods");
        check(cost.missingRequestContainers == 1, "cost: missing request count");
        check(cost.byNamespace.size() == 2
                  && cost.byNamespace.at(0).first == QStringLiteral("dev"),
              "cost: namespaces sorted desc by cost");
        check(cost.topWorkloads.size() == 2
                  && cost.topWorkloads.at(0).pod == QStringLiteral("job-1"),
              "cost: top workload first is the largest");
    }

    {
        QList<ObjectRef> objects;
        objects.append(ObjectRef{QStringLiteral("extensions/v1beta1"),
                                 QStringLiteral("Deployment"), QStringLiteral("default"),
                                 QStringLiteral("old")});
        objects.append(ObjectRef{QStringLiteral("apps/v1"),
                                 QStringLiteral("Deployment"), QStringLiteral("default"),
                                 QStringLiteral("new")});
        objects.append(ObjectRef{QStringLiteral("networking.k8s.io/v1beta1"),
                                 QStringLiteral("Ingress"), QStringLiteral("web"),
                                 QStringLiteral("ing")});
        objects.append(ObjectRef{QStringLiteral("batch/v1beta1"),
                                 QStringLiteral("CronJob"), QStringLiteral("ci"),
                                 QStringLiteral("job")});

        const QList<DeprecationFinding> findings = checkDeprecations(objects);
        check(findings.size() == 3, "deprecation: three deprecated objects");
        bool sawDeployment = false;
        bool sawIngress = false;
        for (const DeprecationFinding &finding : findings) {
            if (finding.kind == QStringLiteral("Deployment")
                && finding.removedIn == QStringLiteral("v1.16.0")
                && finding.replacement == QStringLiteral("apps/v1")) {
                sawDeployment = true;
            }
            if (finding.kind == QStringLiteral("Ingress")
                && finding.removedIn == QStringLiteral("v1.22.0")) {
                sawIngress = true;
            }
        }
        check(sawDeployment, "deprecation: deployment removed mapping");
        check(sawIngress, "deprecation: ingress v1beta1 mapping");
        check(deprecationSummaryText(findings).contains(QStringLiteral("deprecated")),
              "deprecation: summary text");
        check(deprecationDetailText(findings).contains(QStringLiteral("REMOVED")),
              "deprecation: detail marks removed");

        const QByteArray json = QByteArrayLiteral(
            "{\"items\":[{\"apiVersion\":\"extensions/v1beta1\","
            "\"kind\":\"Deployment\",\"metadata\":{\"namespace\":\"default\","
            "\"name\":\"legacy\"},\"spec\":{\"replicas\":1},"
            "\"status\":{\"availableReplicas\":1}}]}");
        bool ok = false;
        const KubeResources res = parseKubectlResources(json, &ok);
        check(ok && res.objects.size() == 1
                  && res.objects.at(0).apiVersion
                         == QStringLiteral("extensions/v1beta1"),
              "kube: parse collects object apiVersion");
    }

    {
        const QByteArray json = QByteArrayLiteral(
            "{\"items\":["
            "{\"kind\":\"Node\",\"metadata\":{\"name\":\"node-1\",\"labels\":{"
            "\"node-role.kubernetes.io/control-plane\":\"\","
            "\"node.kubernetes.io/instance-type\":\"m5.large\","
            "\"topology.kubernetes.io/zone\":\"us-east-1a\","
            "\"topology.kubernetes.io/region\":\"us-east-1\"}},"
            "\"spec\":{\"providerID\":\"aws:///us-east-1a/i-123\"},"
            "\"status\":{\"nodeInfo\":{\"kubeletVersion\":\"v1.30.1\","
            "\"containerRuntimeVersion\":\"containerd://1.7.2\","
            "\"osImage\":\"Ubuntu 24.04\"},"
            "\"capacity\":{\"cpu\":\"8\",\"memory\":\"16384000Ki\",\"pods\":\"110\"},"
            "\"allocatable\":{\"cpu\":\"7900m\",\"memory\":\"15000000Ki\",\"pods\":\"110\"},"
            "\"conditions\":[{\"type\":\"Ready\",\"status\":\"True\"}]}},"
            "{\"kind\":\"Pod\",\"metadata\":{\"namespace\":\"default\",\"name\":\"web\"},"
            "\"spec\":{\"nodeName\":\"node-1\",\"containers\":[]},"
            "\"status\":{\"phase\":\"Running\"}}]}");
        bool ok = false;
        const KubeResources res = parseKubectlResources(json, &ok);
        check(ok && res.nodes.size() == 1, "kube: parsed one node");
        const KubeNode &node = res.nodes.first();
        check(node.name == QStringLiteral("node-1") && node.ready,
              "kube: node name + ready");
        check(node.roles.contains(QStringLiteral("control-plane")),
              "kube: node roles");
        check(node.instanceType == QStringLiteral("m5.large")
                  && node.zone == QStringLiteral("us-east-1a")
                  && node.region == QStringLiteral("us-east-1"),
              "kube: node topology labels");
        check(node.providerId == QStringLiteral("aws:///us-east-1a/i-123"),
              "kube: provisioner id");
        check(node.kubeletVersion == QStringLiteral("v1.30.1")
                  && node.runtime == QStringLiteral("containerd://1.7.2")
                  && node.osImage == QStringLiteral("Ubuntu 24.04"),
              "kube: nodeInfo");
        check(std::fabs(node.cpuCapacityCores - 8.0) < 1e-6
                  && std::fabs(node.cpuAllocatableCores - 7.9) < 1e-6,
              "kube: node cpu capacity/allocatable");
        check(node.memCapacityBytes == 16384000ULL * 1024
                  && node.memAllocatableBytes == 15000000ULL * 1024,
              "kube: node memory capacity/allocatable");
        check(res.pods.at(0).nodeName == QStringLiteral("node-1"),
              "kube: pod nodeName");
        check(kubeNodesText(res.nodes).contains(QStringLiteral("node-1")),
              "kube: nodes text");
    }

    {
        // M65: metrics.k8s.io/v1beta1 shapes (kubectl get --raw), grounded
        // on the shipped kubectl 1.37.1 strings (PodMetricsList kinds,
        // usage/window/containers tags, "Metrics API not available").
        const QByteArray podsMetrics = QByteArrayLiteral(
            "{\"kind\":\"PodMetricsList\",\"apiVersion\":"
            "\"metrics.k8s.io/v1beta1\",\"metadata\":{},\"items\":["
            "{\"kind\":\"PodMetrics\",\"metadata\":{\"name\":\"web-1\","
            "\"namespace\":\"default\",\"creationTimestamp\":null},"
            "\"timestamp\":\"2026-10-24T09:00:00Z\",\"window\":\"30s\","
            "\"containers\":["
            "{\"name\":\"app\",\"usage\":{\"cpu\":\"250m\",\"memory\":\"110Mi\"}},"
            "{\"name\":\"sidecar\",\"usage\":{\"cpu\":\"5m\",\"memory\":\"10Mi\"}}]},"
            "{\"kind\":\"PodMetrics\",\"metadata\":{\"name\":\"cpu-1\","
            "\"namespace\":\"prod\",\"creationTimestamp\":null},\"window\":\"30s\","
            "\"containers\":[{\"name\":\"c\",\"usage\":{\"cpu\":\"2\","
            "\"memory\":\"2048Ki\"}}]}]}");
        bool ok = false;
        const QList<KubeMetricRow> pods =
            parseKubePodMetricsJson(podsMetrics, &ok);
        check(ok && pods.size() == 2, "kube: pod metrics parses");
        check(pods.at(0).key == QStringLiteral("default/web-1")
                  &&std::fabs(pods.at(0).cpuMillis - 255.0) < 1e-6
                  && pods.at(0).memBytes == 110 * 1024 * 1024 + 10 * 1024 * 1024,
              "kube: pod metrics container sums (millicores + bytes)");
        check(pods.at(1).key == QStringLiteral("prod/cpu-1")
                  && std::fabs(pods.at(1).cpuMillis - 2000.0) < 1e-6
                  && pods.at(1).memBytes == 2048 * 1024,
              "kube: pod metrics bare-core + Ki units");

        const QByteArray nodesMetrics = QByteArrayLiteral(
            "{\"kind\":\"NodeMetricsList\",\"apiVersion\":"
            "\"metrics.k8s.io/v1beta1\",\"metadata\":{},\"items\":["
            "{\"kind\":\"NodeMetrics\",\"metadata\":{\"name\":\"node-1\","
            "\"creationTimestamp\":null},\"window\":\"30s\","
            "\"usage\":{\"cpu\":\"750m\",\"memory\":\"3Gi\"}}]}");
        bool nodesOk = false;
        const QList<KubeMetricRow> nodes =
            parseKubeNodeMetricsJson(nodesMetrics, &nodesOk);
        check(nodesOk && nodes.size() == 1
                  && nodes.at(0).key == QStringLiteral("node-1")
                  && std::fabs(nodes.at(0).cpuMillis - 750.0) < 1e-6
                  && nodes.at(0).memBytes == 3ULL * 1024 * 1024 * 1024,
              "kube: node metrics parses");

        bool statusOk = false;
        parseKubePodMetricsJson(
            QByteArrayLiteral("{\"kind\":\"Status\",\"apiVersion\":\"v1\","
                              "\"status\":\"Failure\"}"),
            &statusOk);
        check(!statusOk, "kube: Status payload is not a metrics list");

        bool garbageOk = true;
        parseKubePodMetricsJson(QByteArrayLiteral("not json"), &garbageOk);
        check(!garbageOk, "kube: garbage is not a metrics list");

        // Merge semantics: unknown pods keep the -1 sentinel.
        KubePod pod;
        pod.ns = QStringLiteral("default");
        pod.name = QStringLiteral("web-1");
        KubeNode node;
        node.name = QStringLiteral("node-1");
        QList<KubePod> mergePods{pod};
        QList<KubeNode> mergeNodes{node};
        KubePod orphan;
        orphan.ns = QStringLiteral("default");
        orphan.name = QStringLiteral("other");
        mergePods.append(orphan);
        kubeMergeMetrics(&mergePods, &mergeNodes, pods, nodes);
        check(mergePods.at(0).cpuMillis == 255.0
                  && mergePods.at(0).memBytes
                         == 110L * 1024 * 1024 + 10L * 1024 * 1024,
              "kube: matched pod gains usage depth");
        check(mergePods.at(1).cpuMillis < 0.0 && mergePods.at(1).memBytes < 0,
              "kube: unmatched pod keeps unknown depth sentinel");
        check(mergeNodes.at(0).cpuMillis == 750.0
                  && mergeNodes.at(0).memBytes == 3L * 1024 * 1024 * 1024,
              "kube: matched node gains usage depth");
    }

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
