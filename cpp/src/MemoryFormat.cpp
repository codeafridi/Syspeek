#include "MemoryFormat.h"

#include <QtGlobal>

QString formatMemoryKb(unsigned long long kb) {
    const double mb = static_cast<double>(kb) / 1024.0;

    if (mb >= 1024.0) {
        return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1);
    }

    return QStringLiteral("%1 MB").arg(qRound(mb));
}

QString formatBytesCompact(unsigned long long bytes) {
    if (bytes >= (1ULL << 30)) {
        return QStringLiteral("%1 GB")
            .arg(static_cast<double>(bytes) / (1.0 * (1ULL << 30)), 0, 'f',
                 1);
    }
    if (bytes >= (1ULL << 20)) {
        return QStringLiteral("%1 MB")
            .arg(static_cast<double>(bytes) / (1.0 * (1ULL << 20)), 0, 'f',
                 1);
    }
    if (bytes >= (1ULL << 10)) {
        return QStringLiteral("%1 KB")
            .arg(static_cast<double>(bytes) / (1.0 * (1ULL << 10)), 0, 'f',
                 0);
    }
    return QStringLiteral("%1 B").arg(bytes);
}
