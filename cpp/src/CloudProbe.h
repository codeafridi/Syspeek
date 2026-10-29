#pragma once

#include "CloudStatus.h"

#include <QString>

// Link-local instance metadata probe. Plain blocking HTTP over a raw socket so
// the same code runs from the HUD sampler thread AND from `--dump`, which must
// stay QApplication-free (a QTcpSocket would drag in QtCore's event plumbing
// and, worse, a QApplication on some platforms).
//
// Name cloudProbe rather than cloudSource keeps the file obvious in a stack.
struct CloudProbeOptions {
    bool enabled = true;
    int timeoutMs = 450;
};

struct CloudProbeResult {
    CloudIdentity identity;
    QString error;
    int attempted = 0;
};

CloudProbeResult probeCloudIdentity(const CloudProbeOptions &options =
                                        CloudProbeOptions{});

// Seconds since boot: /proc/uptime, sysctl kern.boottime, GetTickCount64.
// Returns -1 when the platform cannot answer.
qint64 cloudHostUptimeSeconds();
