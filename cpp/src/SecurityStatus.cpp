#include "SecurityStatus.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace {

const char *const kCapabilityNames[] = {
    "CAP_CHOWN",
    "CAP_DAC_OVERRIDE",
    "CAP_DAC_READ_SEARCH",
    "CAP_FOWNER",
    "CAP_FSETID",
    "CAP_KILL",
    "CAP_SETGID",
    "CAP_SETUID",
    "CAP_SETPCAP",
    "CAP_LINUX_IMMUTABLE",
    "CAP_NET_BIND_SERVICE",
    "CAP_NET_BROADCAST",
    "CAP_NET_ADMIN",
    "CAP_NET_RAW",
    "CAP_IPC_LOCK",
    "CAP_IPC_OWNER",
    "CAP_SYS_MODULE",
    "CAP_SYS_RAWIO",
    "CAP_SYS_CHROOT",
    "CAP_SYS_PTRACE",
    "CAP_SYS_PACCT",
    "CAP_SYS_ADMIN",
    "CAP_SYS_BOOT",
    "CAP_SYS_NICE",
    "CAP_SYS_RESOURCE",
    "CAP_SYS_TIME",
    "CAP_SYS_TTY_CONFIG",
    "CAP_MKNOD",
    "CAP_LEASE",
    "CAP_AUDIT_WRITE",
    "CAP_AUDIT_CONTROL",
    "CAP_SETFCAP",
    "CAP_MAC_OVERRIDE",
    "CAP_MAC_ADMIN",
    "CAP_SYSLOG",
    "CAP_WAKE_ALARM",
    "CAP_BLOCK_SUSPEND",
    "CAP_AUDIT_READ",
    "CAP_PERFMON",
    "CAP_BPF",
    "CAP_CHECKPOINT_RESTORE",
};

double capExposure(const QString &name) {
    static const QHash<QString, double> weights = {
        {QStringLiteral("CAP_SYS_ADMIN"), 0.6},
        {QStringLiteral("CAP_SYS_MODULE"), 0.6},
        {QStringLiteral("CAP_SYS_PTRACE"), 0.4},
        {QStringLiteral("CAP_SYS_RAWIO"), 0.4},
        {QStringLiteral("CAP_NET_ADMIN"), 0.4},
        {QStringLiteral("CAP_BPF"), 0.4},
        {QStringLiteral("CAP_DAC_OVERRIDE"), 0.3},
        {QStringLiteral("CAP_DAC_READ_SEARCH"), 0.3},
        {QStringLiteral("CAP_SYS_BOOT"), 0.3},
        {QStringLiteral("CAP_MAC_ADMIN"), 0.3},
        {QStringLiteral("CAP_MAC_OVERRIDE"), 0.3},
        {QStringLiteral("CAP_SETUID"), 0.3},
        {QStringLiteral("CAP_SETGID"), 0.3},
        {QStringLiteral("CAP_SETPCAP"), 0.3},
        {QStringLiteral("CAP_PERFMON"), 0.3},
        {QStringLiteral("CAP_CHECKPOINT_RESTORE"), 0.3},
        {QStringLiteral("CAP_SYS_CHROOT"), 0.2},
        {QStringLiteral("CAP_SYS_TIME"), 0.2},
        {QStringLiteral("CAP_SYS_RESOURCE"), 0.2},
        {QStringLiteral("CAP_MKNOD"), 0.2},
        {QStringLiteral("CAP_AUDIT_CONTROL"), 0.2},
        {QStringLiteral("CAP_SYSLOG"), 0.2},
        {QStringLiteral("CAP_SYS_TTY_CONFIG"), 0.2},
        {QStringLiteral("CAP_IPC_OWNER"), 0.2},
        {QStringLiteral("CAP_LINUX_IMMUTABLE"), 0.2},
        {QStringLiteral("CAP_SETFCAP"), 0.2},
        {QStringLiteral("CAP_FOWNER"), 0.2},
        {QStringLiteral("CAP_NET_RAW"), 0.2},
        {QStringLiteral("CAP_SYS_PACCT"), 0.2},
    };
    return weights.value(name, 0.1);
}

void addFinding(SecurityPosture &posture, const QString &label, double exposure,
                bool hardened) {
    SecurityFinding finding;
    finding.label = label;
    finding.exposure = exposure;
    finding.hardened = hardened;
    posture.findings.append(finding);
}

} // namespace

QStringList decodeCapabilities(quint64 capEff) {
    QStringList out;
    for (int bit = 0; bit < 64; ++bit) {
        if ((capEff & (1ULL << bit)) == 0) {
            continue;
        }
        if (bit < static_cast<int>(sizeof(kCapabilityNames)
                                   / sizeof(kCapabilityNames[0]))) {
            out.append(QString::fromLatin1(kCapabilityNames[bit]));
        } else {
            out.append(QStringLiteral("CAP_%1").arg(bit));
        }
    }
    return out;
}

SecurityPosture evaluateSecurity(const SecurityInput &input) {
    SecurityPosture posture;
    posture.available = true;
    posture.capabilities = decodeCapabilities(input.capEff);

    double exposure = 0.0;

    if (input.capKnown) {
        if (posture.capabilities.isEmpty()) {
            addFinding(posture, QStringLiteral("capabilities dropped"), 0.0,
                       true);
        } else {
            for (const QString &cap : posture.capabilities) {
                const double weight = capExposure(cap);
                exposure += weight;
                addFinding(posture, QStringLiteral("%1 granted").arg(cap),
                           weight, false);
            }
        }
    }

    const double seccompWeight = input.seccomp == 2 ? 0.2
                                 : input.seccomp == 1 ? 0.0
                                                      : 1.0;
    exposure += seccompWeight;
    addFinding(posture,
               input.seccomp == 2 ? QStringLiteral("seccomp filter")
               : input.seccomp == 1 ? QStringLiteral("seccomp strict")
                                    : QStringLiteral("seccomp disabled"),
               seccompWeight, seccompWeight == 0.0);

    if (input.noNewPrivs) {
        addFinding(posture, QStringLiteral("NoNewPrivileges"), 0.0, true);
    } else {
        exposure += 0.4;
        addFinding(posture, QStringLiteral("NoNewPrivileges not set"), 0.4,
                   false);
    }

    if (input.macConfined) {
        addFinding(posture,
                   QStringLiteral("MAC confined (%1)").arg(input.macLabel),
                   0.0, true);
    } else {
        exposure += 0.5;
        addFinding(posture, QStringLiteral("MAC unconfined"), 0.5, false);
    }

    if (input.rootUser) {
        exposure += 0.8;
        addFinding(posture, QStringLiteral("runs as root"), 0.8, false);
    } else {
        addFinding(posture, QStringLiteral("non-root user"), 0.0, true);
    }

    if (input.deletedExe) {
        exposure += 0.3;
        addFinding(posture, QStringLiteral("executable deleted"), 0.3, false);
    }

    if (input.totalNamespaces > 0) {
        const int shared = input.totalNamespaces - input.isolatedNamespaces;
        const double weight =
            static_cast<double>(shared) / input.totalNamespaces * 0.6;
        exposure += weight;
        addFinding(posture,
                   QStringLiteral("namespaces isolated %1/%2")
                       .arg(input.isolatedNamespaces)
                       .arg(input.totalNamespaces),
                   weight, weight == 0.0);
    }

    if (exposure > 10.0) {
        exposure = 10.0;
    }
    posture.exposure = exposure;
    posture.grade = exposure < 2.5   ? QStringLiteral("low")
                    : exposure < 5.0 ? QStringLiteral("medium")
                    : exposure < 7.5 ? QStringLiteral("high")
                                     : QStringLiteral("critical");

    for (const SecurityFinding &finding : posture.findings) {
        if (!finding.hardened && finding.exposure > 0.0) {
            ++posture.exposedCount;
        }
    }
    return posture;
}

SecurityPosture postureFromFindings(const QList<SecurityFinding> &findings) {
    SecurityPosture posture;
    posture.findings = findings;
    posture.available = !findings.isEmpty();
    posture.exposedCount = 0;
    if (!findings.isEmpty()) {
        double total = 0.0;
        for (const SecurityFinding &finding : findings) {
            total += finding.exposure;
            if (!finding.hardened && finding.exposure > 0.0) {
                ++posture.exposedCount;
            }
        }
        posture.exposure = total
                           / static_cast<double>(findings.size());
    }
    posture.grade = posture.exposure < 2.5   ? QStringLiteral("low")
                    : posture.exposure < 5.0 ? QStringLiteral("medium")
                    : posture.exposure < 7.5 ? QStringLiteral("high")
                                             : QStringLiteral("critical");
    return posture;
}

bool readSecurityInput(int pid, SecurityInput &out) {
    const QString base = QStringLiteral("/proc/%1").arg(pid);
    QFile status(base + QStringLiteral("/status"));
    if (!status.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QString text = QString::fromLatin1(status.readAll());
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("CapEff:"))) {
            bool ok = false;
            out.capEff = line.section(QLatin1Char(':'), 1)
                             .trimmed()
                             .toULongLong(&ok, 16);
            out.capKnown = ok;
        } else if (line.startsWith(QStringLiteral("NoNewPrivs:"))) {
            out.noNewPrivs =
                line.section(QLatin1Char(':'), 1).trimmed() == QLatin1String("1");
        } else if (line.startsWith(QStringLiteral("Seccomp:"))) {
            out.seccomp = line.section(QLatin1Char(':'), 1).trimmed().toInt();
        } else if (line.startsWith(QStringLiteral("Uid:"))) {
            const QStringList ids =
                line.section(QLatin1Char(':'), 1)
                    .split(QRegularExpression(QStringLiteral("\\s+")),
                           Qt::SkipEmptyParts);
            if (!ids.isEmpty()) {
                out.rootUser = ids.first() == QLatin1String("0");
            }
        }
    }

    QFile mac(base + QStringLiteral("/attr/current"));
    if (mac.open(QIODevice::ReadOnly)) {
        out.macLabel = QString::fromLatin1(mac.readAll()).trimmed();
        out.macConfined =
            !out.macLabel.isEmpty() && out.macLabel != QLatin1String("unconfined");
    }

    const QString exe = QFile::symLinkTarget(base + QStringLiteral("/exe"));
    out.deletedExe = exe.endsWith(QStringLiteral(" (deleted)"));

    static const QStringList namespaces = {
        QStringLiteral("cgroup"), QStringLiteral("ipc"), QStringLiteral("mnt"),
        QStringLiteral("net"),    QStringLiteral("pid"), QStringLiteral("user"),
        QStringLiteral("uts")};
    int isolated = 0;
    int total = 0;
    for (const QString &ns : namespaces) {
        const QString mine = QFile::symLinkTarget(base + QStringLiteral("/ns/")
                                                  + ns);
        const QString self =
            QFile::symLinkTarget(QStringLiteral("/proc/self/ns/") + ns);
        if (mine.isEmpty() || self.isEmpty()) {
            continue;
        }
        ++total;
        if (mine != self) {
            ++isolated;
        }
    }
    out.isolatedNamespaces = isolated;
    out.totalNamespaces = total;
    return true;
}

QString securitySummaryText(const SecurityPosture &posture) {
    if (!posture.available) {
        return QStringLiteral("security: unavailable");
    }
    return QStringLiteral("exposure %1/10 (%2)  %3 finding(s)")
        .arg(QString::number(posture.exposure, 'f', 1), posture.grade)
        .arg(posture.exposedCount);
}

QString securityDetailText(const SecurityPosture &posture) {
    if (!posture.available) {
        return QStringLiteral("security: unavailable");
    }
    QStringList lines;
    for (const SecurityFinding &finding : posture.findings) {
        lines.append(QStringLiteral("%1  %2%3")
                         .arg(finding.hardened ? QStringLiteral("ok  ")
                                               : QStringLiteral("EXPOSED"),
                              finding.exposure > 0.0
                                  ? QStringLiteral("%1  ")
                                        .arg(QString::number(finding.exposure,
                                                             'f', 1))
                                  : QString(),
                              finding.label));
    }
    return lines.join(QLatin1Char('\n'));
}
