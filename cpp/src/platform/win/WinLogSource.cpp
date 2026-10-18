#include "WinLogSource.h"

#if defined(Q_OS_WIN)
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <vector>
#include <windows.h>
#include <winevt.h>

namespace {

int eventLevelToPriority(unsigned level) {
    switch (level) {
    case 1: // Critical -> journal critical (mac Fault maps to 2)
        return 2;
    case 2: // Error -> journal err
        return 3;
    case 3: // Warning -> journal warning
        return 4;
    case 5: // Verbose/Debug -> journal debug
        return 7;
    case 4: // Information
    default:
        return 6;
    }
}

QString normalizedEventTime(const QString &raw) {
    static const QRegularExpression re(QRegularExpression::anchoredPattern(
        QStringLiteral("(\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2})"
                       "(?:\\.(\\d+))?(Z|[+-]\\d{2}:?\\d{2})")));
    const QRegularExpressionMatch match = re.match(raw.trimmed());
    if (!match.hasMatch()) {
        return raw.trimmed();
    }
    QString fraction = match.captured(2);
    if (fraction.size() > 3) {
        fraction.truncate(3);
    }
    while (fraction.size() < 3) {
        fraction.append(QLatin1Char('0'));
    }
    return match.captured(1) + QLatin1Char('.') + fraction
        + match.captured(3);
}

qint64 eventTimeMs(const QString &systemTime) {
    const QDateTime stamp =
        QDateTime::fromString(normalizedEventTime(systemTime),
                              Qt::ISODateWithMs);
    return stamp.isValid() ? stamp.toMSecsSinceEpoch() : 0;
}

QString quotedAttr(const QString &element, const char *name) {
    static const QRegularExpression re(
        QStringLiteral("%1=['\"]([^'\">]+)['\"]").arg(name));
    const QRegularExpressionMatch match = re.match(element);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString messageText(EVT_HANDLE event, EVT_HANDLE metadata) {
    std::vector<wchar_t> buffer(2048);
    DWORD used = 0;
    BOOL ok = ::EvtFormatMessage(
        metadata, event, 0, 0, nullptr, EvtFormatMessageEvent,
        DWORD(buffer.size() * sizeof(wchar_t)), buffer.data(), &used);
    if (!ok && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        buffer.resize(used / sizeof(wchar_t) + 1);
        ok = ::EvtFormatMessage(
            metadata, event, 0, 0, nullptr, EvtFormatMessageEvent,
            DWORD(buffer.size() * sizeof(wchar_t)), buffer.data(), &used);
    }
    if (!ok) {
        return QString();
    }
    return QString::fromWCharArray(buffer.data());
}

} // namespace

bool fetchEventLogJournalJson(QByteArray &out, int limitEvents,
                              int windowSeconds) {
    Q_UNUSED(windowSeconds);

    struct Entry {
        qint64 ts = 0;
        QJsonObject object;
    };
    QList<Entry> entries;

    for (const wchar_t *channel : {L"System", L"Application"}) {
        const EVT_HANDLE query =
            ::EvtQuery(nullptr, channel, L"*",
                       EvtQueryChannelPath | EvtQueryReverseDirection);
        if (query == nullptr) {
            continue;
        }

        QHash<QString, EVT_HANDLE> metadataCache;
        std::vector<EVT_HANDLE> batch(64);
        const int perChannelLimit = qMax(1, limitEvents / 2);
        int count = 0;

        while (count < perChannelLimit) {
            DWORD received = 0;
            if (!::EvtNext(query, DWORD(batch.size()), batch.data(), 3000, 0,
                           &received)
                || received == 0) {
                break;
            }

            for (DWORD i = 0; i < received && count < perChannelLimit; ++i) {
                const EVT_HANDLE event = batch[i];

                // EvtRender gives the event XML; parse the System properties.
                DWORD size = 0;
                DWORD propertyCount = 0;
                if (!::EvtRender(nullptr, event, EvtRenderEventXml, 0, nullptr,
                                 &size, &propertyCount)
                    && ::GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
                    std::vector<wchar_t> xml(size);
                    if (::EvtRender(nullptr, event, EvtRenderEventXml, size,
                                    xml.data(), &size, &propertyCount)) {
                        const QString xmlText = QString::fromWCharArray(
                            xml.data(), int(size / sizeof(wchar_t)) - 1);

                        const QRegularExpressionMatch system =
                            QRegularExpression(
                                QStringLiteral(
                                    "<System[^>]*>(.*?)</System>"),
                                QRegularExpression::DotMatchesEverythingOption)
                                .match(xmlText);
                        const QString sys =
                            system.hasMatch() ? system.captured(1)
                                              : xmlText;

                        const QString timeStr =
                            quotedAttr(sys, "SystemTime");
                        const QString provider =
                            quotedAttr(sys, "Name"); // <Provider Name=...> (+
                                                     // Guid/DOTLESS paths)
                        const QRegularExpressionMatch levelMatch =
                            QRegularExpression(
                                QStringLiteral("<Level>(\\d+)</Level>"))
                                .match(sys);
                        unsigned level = 4;
                        if (levelMatch.hasMatch()) {
                            level = levelMatch.captured(1).toUInt();
                        }
                        const QRegularExpressionMatch pidMatch =
                            QRegularExpression(
                                QStringLiteral(
                                    "<Execution[^>]*ProcessID=['\"](\\d+)['\"]"))
                                .match(sys);
                        const QRegularExpressionMatch idMatch =
                            QRegularExpression(
                                QStringLiteral("<EventID[^>]*>(\\d+)</EventID>"))
                                .match(sys);

                        EVT_HANDLE meta = metadataCache.value(
                            provider, nullptr);
                        if (meta == nullptr && !provider.isEmpty()) {
                            meta = ::EvtOpenPublisherMetadata(
                                nullptr,
                                reinterpret_cast<const wchar_t *>(
                                    provider.utf16()),
                                nullptr, 0, 0);
                            if (meta != nullptr) {
                                metadataCache.insert(provider, meta);
                            }
                        }

                        QString message = messageText(event, meta);
                        if (message.isEmpty()) {
                            message = QStringLiteral("%1 event %2")
                                          .arg(QString::fromWCharArray(
                                                   channel),
                                               idMatch.captured(1));
                        }

                        Entry entry;
                        entry.ts = eventTimeMs(timeStr);
                        entry.object.insert(
                            QStringLiteral("__REALTIME_TIMESTAMP"),
                            entry.ts);
                        entry.object.insert(
                            QStringLiteral("PRIORITY"),
                            eventLevelToPriority(level));
                        entry.object.insert(
                            QStringLiteral("_PID"),
                            pidMatch.hasMatch()
                                ? pidMatch.captured(1).toInt()
                                : 0);
                        entry.object.insert(
                            QStringLiteral("_COMM"), provider);
                        entry.object.insert(
                            QStringLiteral("MESSAGE"), message);
                        entries.append(entry);
                    }
                }

                ::EvtClose(event);
                ++count;
            }
        }

        for (const EVT_HANDLE meta : metadataCache) {
            ::EvtClose(meta);
        }
        ::EvtClose(query);
    }

    if (entries.isEmpty()) {
        out.clear();
        return false;
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.ts < b.ts; });

    out.clear();
    for (const Entry &entry : entries) {
        out.append(QJsonDocument(entry.object)
                       .toJson(QJsonDocument::Compact));
        out.append('\n');
    }
    return true;
}
#endif
