#include "DockerEngineClient.h"
#include "DockerStatus.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QHash>
#include <QStringList>
#include <cstdio>

static QString liveNamesCellValue(const QList<DockerContainer> &rows,
                                  const QString &id) {
    for (const DockerContainer &row : rows) {
        if (row.id == id) {
            return row.name;
        }
    }
    return id;
}

namespace {

DockerSnapshot okSnapshot(const QList<DockerContainer> &containers) {
    DockerSnapshot s;
    s.state = DockerState::Ok;
    s.containers = containers;
    s.runningCount = dockerRunningCount(containers);
    s.issueCount = dockerIssueCount(containers);
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

    bool ok = false;
    const QByteArray out = QByteArrayLiteral(
        "id1\tweb\trunning\tUp 2 days (healthy)\n"
        "id2\tdb\trunning\tUp 5 minutes (unhealthy)\n"
        "id3\tjob\trestarting\tRestarting (1) 5 seconds ago\n"
        "id4\told\texited\tExited (0) 2 days ago\n");

    const QList<DockerContainer> containers = parseDockerPs(out, &ok);
    check(ok, "valid tab-separated output parses (ok)");
    check(containers.size() == 4, "parsed four containers");
    check(containers.at(0).id == QStringLiteral("id1"), "parsed id");
    check(containers.at(0).name == QStringLiteral("web"), "parsed name");
    check(containers.at(0).state == QStringLiteral("running"), "parsed state");
    check(containers.at(0).health == QStringLiteral("healthy"),
          "parsed healthy health");
    check(containers.at(1).health == QStringLiteral("unhealthy"),
          "parsed unhealthy health");

    check(dockerRunningCount(containers) == 2,
          "running count excludes restarting/exited");
    check(dockerIssueCount(containers) == 2,
          "issue count = unhealthy + restarting");
    check(!dockerContainerIsIssue(containers.at(0)), "healthy is not an issue");
    check(dockerContainerIsIssue(containers.at(2)), "restarting is an issue");

    bool emptyOk = false;
    check(parseDockerPs(QByteArrayLiteral(""), &emptyOk).isEmpty() && emptyOk,
          "empty output parses to no containers");
    check(parseDockerPs(QByteArrayLiteral("\n\n"), &emptyOk).isEmpty(),
          "blank lines ignored");

    bool malformedOk = true;
    check(parseDockerPs(QByteArrayLiteral("a\tb\tc"), &malformedOk).isEmpty()
              && !malformedOk,
          "malformed (3 fields) -> parse failed");

    check(classifyDockerFailure(
              QByteArrayLiteral("permission denied while trying to connect"))
              == DockerState::PermissionDenied,
          "permission error -> permission denied");
    check(classifyDockerFailure(
              QByteArrayLiteral("Cannot connect to the Docker daemon at "
                                "unix:///var/run/docker.sock."))
              == DockerState::Unreachable,
          "daemon error -> unreachable");
    check(classifyDockerFailure(QByteArrayLiteral("something unexpected went wrong"))
              == DockerState::CommandFailed,
          "other error -> command failed (not unreachable)");

    check(dockerSummaryText(DockerSnapshot{})
              == QStringLiteral("docker: not installed - install Docker "
                                "Engine or Docker Desktop to see containers"),
          "NotInstalled -> explicit install hint (M58, was silent-empty)");

    DockerSnapshot unreachable;
    unreachable.state = DockerState::Unreachable;
    check(dockerSummaryText(unreachable)
              == QStringLiteral("docker: daemon unreachable"),
          "unreachable summary");

    DockerSnapshot denied;
    denied.state = DockerState::PermissionDenied;
    check(dockerSummaryText(denied) == QStringLiteral("docker: permission denied"),
          "permission denied summary");

    DockerSnapshot timedOut;
    timedOut.state = DockerState::TimedOut;
    check(dockerSummaryText(timedOut) == QStringLiteral("docker: timed out"),
          "timed out summary");

    DockerSnapshot cmdFailed;
    cmdFailed.state = DockerState::CommandFailed;
    check(dockerSummaryText(cmdFailed)
              == QStringLiteral("docker: command failed"),
          "command failed summary");

    DockerSnapshot parseFailed;
    parseFailed.state = DockerState::ParseFailed;
    check(dockerSummaryText(parseFailed)
              == QStringLiteral("docker: parse failed"),
          "parse failed summary");

    DockerSnapshot oneRunning;
    oneRunning.state = DockerState::Ok;
    oneRunning.runningCount = 1;
    check(dockerSummaryText(oneRunning) == QStringLiteral("docker: 1 running"),
          "ok summary no issues");

    DockerSnapshot oneIssue;
    oneIssue.state = DockerState::Ok;
    oneIssue.runningCount = 2;
    oneIssue.issueCount = 1;
    check(dockerSummaryText(oneIssue)
              == QStringLiteral("docker: 2 running | 1 issue"),
          "ok summary singular issue");

    DockerSnapshot twoIssues;
    twoIssues.state = DockerState::Ok;
    twoIssues.runningCount = 3;
    twoIssues.issueCount = 2;
    check(dockerSummaryText(twoIssues)
              == QStringLiteral("docker: 3 running | 2 issues"),
          "ok summary plural issues");

    DockerSnapshot stale;
    stale.state = DockerState::Stale;
    stale.stale = true;
    stale.runningCount = 1;
    stale.containers = containers;
    check(dockerSummaryText(stale)
              == QStringLiteral("docker: 1 running | stale"),
          "stale summary keeps last counts");

    const auto healthyOnly = okSnapshot(
        parseDockerPs(QByteArrayLiteral("id1\tweb\trunning\tUp (healthy)\n"),
                      &ok));
    check(dockerDetailText(healthyOnly) == QStringLiteral("no issues"),
          "detail text: no issues");

    const auto withIssue = okSnapshot(containers);
    const QString detail = dockerDetailText(withIssue);
    check(detail.contains(QStringLiteral("db")),
          "detail text lists unhealthy container");
    check(detail.contains(QStringLiteral("job")),
          "detail text lists restarting container");
    check(!detail.contains(QStringLiteral("web")),
          "detail text omits healthy container");

    check(parseDockerExitCode(QStringLiteral("Exited (137) 2 minutes ago")) == 137,
          "parse exit code 137");
    check(parseDockerExitCode(QStringLiteral("Up 2 days (healthy)")) == -1,
          "no exit code for running container");
    check(parseDockerExitCode(QStringLiteral("Restarting (3) 5 seconds ago")) == 3,
          "parse parenthesised count");

    {
        DockerStateTracker tracker;

        QList<DockerContainer> running;
        DockerContainer a;
        a.id = QStringLiteral("a");
        a.name = QStringLiteral("svc");
        a.state = QStringLiteral("running");
        running.append(a);
        tracker.update(running);
        check(!running.at(0).crashed, "running container not crashed");

        QList<DockerContainer> exited;
        DockerContainer b = a;
        b.state = QStringLiteral("exited");
        b.status = QStringLiteral("Exited (137) 1 second ago");
        b.exitCode = parseDockerExitCode(b.status);
        exited.append(b);
        tracker.update(exited);
        check(exited.at(0).crashed, "running->exited(137) is a crash");
        check(dockerContainerIsIssue(exited.at(0)),
              "crashed container is an issue");
        check(dockerDetailText(okSnapshot(exited)).contains(
                  QStringLiteral("recently exited")),
              "detail marks recently exited");

        tracker.update(running);
        check(!running.at(0).crashed, "restarted container clears crash flag");

        QList<DockerContainer> fresh;
        DockerContainer c;
        c.id = QStringLiteral("c");
        c.state = QStringLiteral("exited");
        c.status = QStringLiteral("Exited (1) 1 second ago");
        c.exitCode = 1;
        fresh.append(c);
        tracker.update(fresh);
        check(!fresh.at(0).crashed,
              "never-observed-running exit is not a crash");
    }

    // --- M64 engine API models -----------------------------
    const QByteArray containersJson = QByteArrayLiteral(R"json([
        {"Id":"c1","Names":["/web"],"Image":"nginx:alpine","State":"running",
         "Status":"Up 2 days (healthy)"},
        {"Id":"c2","Names":["/db"],"Image":"pg:16","State":"running",
         "Status":"Up 5 minutes"},
        {"Id":"c3","Names":["/job"],"Image":"alpine","State":"exited",
         "Status":"Exited (0) 2 days ago"}
    ])json");
    QString engineParseError;
    const QList<DockerContainer> parsed =
        parseDockerContainersJson(containersJson, &engineParseError);
    check(parsed.size() == 3, "containers json parses three rows");
    check(parsed.at(0).name == QStringLiteral("web"), "engine name strips /");
    check(parsed.at(0).image == QStringLiteral("nginx:alpine"),
          "engine image captured");
    check(parsed.at(0).state == QStringLiteral("running"),
          "engine state captured");
    check(parsed.at(0).health == QStringLiteral("healthy"),
          "engine status-derived health text");
    check(parsed.at(2).exitCode == 0, "engine exited status exit code");

    const QByteArray statsJson = QByteArrayLiteral(R"json({
        "read": "2026-10-24T08:00:00Z",
        "cpu_stats": {"cpu_usage": {"total_usage": 88000000000},
                      "system_cpu_usage": 22000000000000,
                      "online_cpus": 8},
        "precpu_stats": {"cpu_usage": {"total_usage": 80300000000},
                         "system_cpu_usage": 21800000000000},
        "memory_stats": {"usage": 234000000, "limit": 4000000000},
        "networks": {"eth0": {"rx_bytes": 1048576, "tx_bytes": 2048}},
        "blkio_stats": {"io_service_bytes_recursive": [
            {"op": "read", "value": 512000},
            {"op": "write", "value": 4096}]},
        "pids_stats": {"current": 12}
    })json");
    DockerDepth depth;
    check(parseDockerStatsJson(statsJson, &depth), "stats json parses");
    check(depth.cpuCores == 8, "stats online_cpus");
    check(depth.cpuPercent > 30.7 && depth.cpuPercent < 30.9,
          "stats cpu percent (delta cpu 7.7e9 / system 2e11 * 8 cores = 30.8)");
    check(depth.hasMem && depth.memUsedBytes == 234000000
              && depth.memLimitBytes == 4000000000,
          "stats memory fields");
    check(depth.hasNet && depth.netRxBytes == 1048576
              && depth.netTxBytes == 2048,
          "stats network sums across interfaces");
    check(depth.hasBlk && depth.blkReadBytes == 512000
              && depth.blkWriteBytes == 4096,
          "stats blkio read/write ops");
    check(depth.pids == 12, "stats pids");

    DockerDepth degenerate;
    check(parseDockerStatsJson(QByteArrayLiteral(
              "{\"cpu_stats\":{\"cpu_usage\":{\"total_usage\":100},"
              "\"online_cpus\":4,\"system_cpu_usage\":50},"
              "\"precpu_stats\":{\"cpu_usage\":{\"total_usage\":100},"
              "\"system_cpu_usage\":50},"
              "\"memory_stats\":{\"usage\":0}}"),
          &degenerate),
          "degenerate stats still parse");
    check(degenerate.cpuPercent < 0.0,
          "zero system delta leaves cpu unknown (-1)");
    check(!degenerate.hasMem, "zero usage marks memory absent");
    check(!parseDockerStatsJson(
              QByteArrayLiteral(
                  "{\"message\":\"no such container: zzz\"}"),
              &degenerate),
          "engine error message rejects stats parse");

    // --- Transport: live Docker Engine roundtrip when a local engine socket
    // exists (CI runners without docker skip this; dev boxes prove it).
#if !defined(Q_OS_WIN)
    const QString liveSocket =
        QStringLiteral("/var/run/docker.sock");
    if (QFileInfo::exists(liveSocket)) {
        DockerEngineClient client(liveSocket, 3000);
        QByteArray fetched;
        QString liveError;
        if (client.get(QStringLiteral("/containers/json?all=1"), &fetched,
                       &liveError)) {
            QString liveParseError;
            const QList<DockerContainer> rows =
                parseDockerContainersJson(fetched, &liveParseError);
            check(liveParseError.isEmpty(),
                  "live engine containers json parses");
            std::printf("live engine ok: %lld rows\n",
                        static_cast<long long>(rows.size()));
            QStringList runningIds;
            for (const DockerContainer &row : rows) {
                if (row.state == QLatin1String("running") && runningIds.size() < 3) {
                    runningIds.append(row.id);
                }
            }
            if (!runningIds.isEmpty()) {
                QElapsedTimer statsClock;
                statsClock.start();
                const QHash<QString, QByteArray> statsBodies =
                    client.statsMulti(runningIds, 6000, &liveError);
                const qint64 wallMs = statsClock.elapsed();
                for (const QString &id : runningIds) {
                    const QByteArray raw = statsBodies.value(id);
                    if (raw.isEmpty()) {
                        continue;
                    }
                    DockerDepth liveDepth;
                    if (parseDockerStatsJson(raw, &liveDepth)) {
                        std::printf(
                            "live stats ok (multi): %s cpu=%.1f cores=%d pids=%d\n",
                            liveNamesCellValue(rows, id).toLatin1().constData(),
                            liveDepth.cpuPercent, liveDepth.cpuCores,
                            liveDepth.pids);
                    }
                }
                std::printf(
                    "multiplexed stats wall: %lld ms for %lld requests\n",
                    wallMs, static_cast<long long>(runningIds.size()));
            }

            // Lifecycle verbs (M78). A verb only answers once the container
            // moved state and dockerd's graceful-stop grace is 10 s, so this
            // leg runs on its own budget: with the list/stats timeout a
            // correct restart reads as a failure (seen live before the fix).
            DockerEngineClient verbClient(liveSocket, 30000);
            QString verbError;
            const bool missingRefused = !verbClient.post(
                QStringLiteral("/containers/syspeek-no-such-id/restart"),
                nullptr, &verbError, 30000);
            check(missingRefused,
                  "live engine: a lifecycle verb on a missing id is refused");

            QProcess create;
            create.start(QStringLiteral("docker"),
                         {QStringLiteral("create"), QStringLiteral("--name"),
                          QStringLiteral("syspeek-m78-test"),
                          QStringLiteral("debian:12"),
                          QStringLiteral("sleep"), QStringLiteral("120")});
            const bool created = create.waitForFinished(15000)
                                 && create.exitCode() == 0;
            if (created) {
                const QString id = QString::fromUtf8(
                    create.readAllStandardOutput()).trimmed();
                QProcess inspect;
                const auto stateOf = [&inspect](const QString &target) {
                    inspect.start(QStringLiteral("docker"),
                                  {QStringLiteral("inspect"),
                                   QStringLiteral("-f"),
                                   QStringLiteral("{{.State.Status}}"), target});
                    inspect.waitForFinished(5000);
                    return QString::fromUtf8(inspect.readAllStandardOutput())
                        .trimmed();
                };
                QElapsedTimer verbClock;
                verbClock.start();
                const bool started = verbClient.post(
                    QStringLiteral("/containers/%1/start").arg(id), nullptr,
                    &verbError, 30000);
                const qint64 startMs = verbClock.elapsed();
                check(started, "live engine: POST start succeeds");
                check(stateOf(id) == QLatin1String("running"),
                      "live engine: the container is running after start");
                verbClock.restart();
                const bool stopped = verbClient.post(
                    QStringLiteral("/containers/%1/stop").arg(id), nullptr,
                    &verbError, 30000);
                const qint64 stopMs = verbClock.elapsed();
                check(stopped, "live engine: POST stop succeeds");
                check(stateOf(id) == QLatin1String("exited"),
                      "live engine: the container is exited after stop");
                std::printf(
                    "live lifecycle ok: start %lld ms, stop %lld ms\n",
                    startMs, stopMs);
                QProcess cleanup;
                cleanup.start(QStringLiteral("docker"),
                              {QStringLiteral("rm"), QStringLiteral("-f"), id});
                cleanup.waitForFinished(15000);
            } else {
                std::printf("SKIP live lifecycle: docker create unavailable\n");
            }
        } else if (liveError.contains(QStringLiteral("Permission denied"))) {
            std::printf("SKIP live engine: permission denied in CI\n");
        } else {
            std::printf("SKIP live engine: %s\n",
                        liveError.toUtf8().constData());
        }
    } else {
        std::printf("SKIP live engine: no local engine socket\n");
    }
#endif

    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
