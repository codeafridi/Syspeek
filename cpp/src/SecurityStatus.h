#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

struct SecurityInput {
    quint64 capEff = 0;
    bool capKnown = false;
    int seccomp = 0; // 0 none, 1 strict, 2 filter
    bool noNewPrivs = false;
    bool macConfined = false;
    QString macLabel;
    bool deletedExe = false;
    bool rootUser = false;
    int isolatedNamespaces = 0;
    int totalNamespaces = 0;
};

struct SecurityFinding {
    QString label;
    double exposure = 0.0;
    bool hardened = false;
};

struct SecurityPosture {
    bool available = false;
    double exposure = 0.0; // 0..10, systemd-analyze style
    QString grade;
    int exposedCount = 0;
    QStringList capabilities;
    QList<SecurityFinding> findings;
};

// Decode a capability bitmask into CAP_* names.
QStringList decodeCapabilities(quint64 capEff);

// Pure scoring (testable), modelled on systemd-analyze security exposure.
SecurityPosture evaluateSecurity(const SecurityInput &input);

// Pure host-posture scoring for OS-tool-derived facts (macOS: SIP,
// FileVault, Gatekeeper, application firewall).
SecurityPosture postureFromFindings(const QList<SecurityFinding> &findings);

// Read /proc/<pid> hardening facts (capabilities, seccomp, namespaces, ...).
bool readSecurityInput(int pid, SecurityInput &out);

QString securitySummaryText(const SecurityPosture &posture);
QString securityDetailText(const SecurityPosture &posture);
