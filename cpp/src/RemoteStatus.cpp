#include "RemoteStatus.h"

#include <QDir>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString refuse(QString *error, const QString &reason) {
    if (error != nullptr) {
        *error = reason;
    }
    return QString();
}

} // namespace

void remoteValidate(RemoteOptions &options) {
    options.intervalMs = std::clamp(options.intervalMs, kRemoteIntervalMinMs,
                                    kRemoteIntervalMaxMs);
    options.timeoutMs = std::clamp(options.timeoutMs, kRemoteTimeoutMinMs,
                                   kRemoteTimeoutMaxMs);
    options.spoolMb =
        std::clamp(options.spoolMb, kRemoteSpoolMinMb, kRemoteSpoolMaxMb);
}

QString remoteNormalizeUrl(const QString &spec, QString *error) {
    if (error != nullptr) {
        error->clear();
    }

    QString rest = spec.trimmed();
    if (rest.isEmpty()) {
        return refuse(error, QStringLiteral("empty relay url"));
    }

    QString scheme = QStringLiteral("https");
    const QString separator = QStringLiteral("://");
    const int schemeEnd = rest.indexOf(separator);
    if (schemeEnd >= 0) {
        const QString declared = rest.left(schemeEnd).toLower();
        if (declared != QLatin1String("http") && declared != QLatin1String("https")) {
            return refuse(error, QStringLiteral("%1 is not http or https")
                                     .arg(declared + separator));
        }
        scheme = declared;
        rest = rest.mid(schemeEnd + separator.size());
    }

    if (rest.contains(QLatin1Char('?'))) {
        return refuse(error,
                      QStringLiteral("a query string is not part of a relay address"));
    }
    if (rest.contains(QLatin1Char('#'))) {
        return refuse(error,
                      QStringLiteral("a fragment is not part of a relay address"));
    }
    if (rest.contains(QLatin1Char('@'))) {
        return refuse(error,
                      QStringLiteral("credentials in the url are refused - use "
                                     "[remote] token="));
    }
    if (rest.contains(QLatin1Char(' ')) || rest.contains(QLatin1Char('\t'))) {
        return refuse(error, QStringLiteral("whitespace inside the relay url"));
    }

    QString path;
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash >= 0) {
        path = rest.mid(slash);
        rest = rest.left(slash);
    }
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    if (path == QLatin1String("/")) {
        path.clear();
    }

    QString host;
    QString port;
    if (rest.startsWith(QLatin1Char('['))) {
        const int close = rest.indexOf(QLatin1Char(']'));
        if (close < 0) {
            return refuse(error, QStringLiteral("unterminated IPv6 literal"));
        }
        host = rest.left(close + 1);
        const QString tail = rest.mid(close + 1);
        if (!tail.isEmpty()) {
            if (!tail.startsWith(QLatin1Char(':'))) {
                return refuse(error,
                              QStringLiteral("unexpected text after the IPv6 "
                                             "literal"));
            }
            port = tail.mid(1);
        }
    } else {
        const int colon = rest.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) {
            host = rest.left(colon);
            port = rest.mid(colon + 1);
            if (host.contains(QLatin1Char(':'))) {
                return refuse(error,
                              QStringLiteral("IPv6 literals need brackets"));
            }
        } else {
            host = rest;
        }
    }

    if (host.isEmpty()) {
        return refuse(error, QStringLiteral("no host in the relay url"));
    }
    if (host == QLatin1String("[]")) {
        return refuse(error, QStringLiteral("no host in the relay url"));
    }

    if (!port.isEmpty()) {
        bool ok = false;
        const int parsed = port.toInt(&ok);
        if (!ok || parsed < 1 || parsed > 65535) {
            return refuse(error, QStringLiteral("port %1 is not 1..65535").arg(port));
        }
        host += QLatin1Char(':') + QString::number(parsed);
    } else if (rest.endsWith(QLatin1Char(':'))) {
        return refuse(error, QStringLiteral("port is empty"));
    }

    return scheme + separator + host + path;
}

QString remoteIngestUrl(const QString &base, QString *error) {
    const QString normalized = remoteNormalizeUrl(base, error);
    if (normalized.isEmpty()) {
        return QString();
    }
    const QString ingest = QStringLiteral("/api/v1/ingest");
    if (normalized.endsWith(ingest)) {
        return normalized;
    }
    return normalized + ingest;
}

bool remoteConfigUsable(const RemoteOptions &options) {
    if (!options.enabled || options.token.isEmpty()) {
        return false;
    }
    return !remoteNormalizeUrl(options.url, nullptr).isEmpty();
}

QString remoteSpoolPath() {
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/syspeek");
    }
    return base + QStringLiteral("/remote-spool.bin");
}

QString remoteConfigNote(const RemoteOptions &options) {
    if (!options.enabled) {
        return QStringLiteral("remote: off - nothing leaves this machine");
    }
    QString error;
    const QString base = remoteNormalizeUrl(options.url, &error);
    if (base.isEmpty()) {
        return error.isEmpty()
                   ? QStringLiteral("remote: no relay address - set [remote] url=")
                   : QStringLiteral("remote: %1 (set [remote] url=)").arg(error);
    }
    if (options.token.isEmpty()) {
        return QStringLiteral("remote: configured for %1 but no token - the relay "
                              "will refuse it (set [remote] token=)")
            .arg(base);
    }
    return QStringLiteral("remote: configured for %1 - transport not started in "
                          "this build")
        .arg(base);
}
